/*
gl_openxr.c - Linux/GLX OpenXR compositor for ref_gl
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#if XASH_OPENXR

#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL

#include <X11/Xlib.h>
#include <X11/Xutil.h>

/* Avoid including the system GL headers, which conflict with ref_gl's GL shim. */
typedef XID GLXDrawable;
typedef struct __GLXcontextRec *GLXContext;
typedef struct __GLXFBConfigRec *GLXFBConfig;

#ifndef GLX_FBCONFIG_ID
#define GLX_FBCONFIG_ID 0x8013
#endif

extern Display *glXGetCurrentDisplay( void );
extern GLXContext glXGetCurrentContext( void );
extern GLXDrawable glXGetCurrentDrawable( void );
extern int glXQueryContext( Display *display, GLXContext context, int attribute, int *value );
extern GLXFBConfig *glXChooseFBConfig( Display *display, int screen, const int *attributes, int *count );
extern XVisualInfo *glXGetVisualFromFBConfig( Display *display, GLXFBConfig config );

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdlib.h>
#include <string.h>

#include "gl_local.h"

/* gl_export.h intentionally exposes only the legacy constants used by ref_gl. */
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_COLOR_CLEAR_VALUE
#define GL_COLOR_CLEAR_VALUE 0x0C22
#endif
#ifndef GL_SCISSOR_BOX
#define GL_SCISSOR_BOX 0x0C10
#endif

typedef struct gl_openxr_eye_s
{
	XrSwapchain swapchain;
	uint32_t width;
	uint32_t height;
	uint32_t image_count;
	uint32_t image_index;
	XrSwapchainImageOpenGLKHR *images;
	GLuint *framebuffers;
	qboolean acquired;
	qboolean waited;
	qboolean ready;
} gl_openxr_eye_t;

typedef struct gl_openxr_actions_s
{
	XrActionSet gameplay_set;
	XrPath hand_paths[REF_VR_MAX_HANDS];
	XrAction grip_pose;
	XrAction aim_pose;
	XrAction trigger;
	XrAction squeeze;
	XrAction stick;
	XrAction primary;
	XrAction secondary;
	XrAction menu;
	XrAction stick_click;
	XrAction haptic;
	XrSpace grip_spaces[REF_VR_MAX_HANDS];
	XrSpace aim_spaces[REF_VR_MAX_HANDS];
} gl_openxr_actions_t;

typedef struct gl_openxr_binding_s
{
	XrAction action;
	const char *left_path;
	const char *right_path;
} gl_openxr_binding_t;

typedef struct gl_openxr_state_s
{
	qboolean initialized;
	qboolean failed;
	qboolean session_running;
	qboolean focused;
	qboolean frame_begun;
	qboolean views_valid;
	qboolean submitted_once;
	qboolean ui_submitted_once;
	qboolean reference_changed;
	qboolean exit_requested;
	int current_eye;
	uint64_t frame_id;

	XrInstance instance;
	XrSystemId system_id;
	XrSession session;
	XrSessionState session_state;
	XrSpace local_space;
	XrSpace head_space;
	XrFrameState frame_state;
	XrView views[REF_VR_MAX_EYES];
	XrPosef head_pose;
	gl_openxr_eye_t eyes[REF_VR_MAX_EYES];
	gl_openxr_eye_t ui;
	gl_openxr_actions_t actions;
	qboolean ui_active;
	int ui_source_width;
	int ui_source_height;
	float ui_clear_color[4];
	float comfort_level;
	uint64_t comfort_frame;
} gl_openxr_state_t;

static gl_openxr_state_t xr;
static cvar_t *vr_enable;
static cvar_t *vr_supersampling;
static cvar_t *vr_worldscale;
static cvar_t *vr_ui_distance;
static cvar_t *vr_ui_width;
static cvar_t *vr_comfort_mask;
static cvar_t *vr_comfort_moving;
static cvar_t *vr_scope_engaged;

void GL_OpenXRInit( void )
{
	vr_enable = gEngfuncs.Cvar_Get( "vr_enable", "0", FCVAR_ARCHIVE, "enable desktop OpenXR rendering" );
	vr_supersampling = gEngfuncs.Cvar_Get( "vr_supersampling", "1.0", FCVAR_ARCHIVE, "OpenXR eye resolution scale" );
	vr_worldscale = gEngfuncs.Cvar_Get( "vr_worldscale", "40", FCVAR_ARCHIVE, "game units per physical metre" );
	vr_ui_distance = gEngfuncs.Cvar_Get( "vr_ui_distance", "1.5", FCVAR_ARCHIVE, "head-relative VR UI distance in metres" );
	vr_ui_width = gEngfuncs.Cvar_Get( "vr_ui_width", "2.0", FCVAR_ARCHIVE, "head-relative VR UI width in metres" );
	vr_comfort_mask = gEngfuncs.Cvar_Get( "vr_comfort_mask", "0.0", FCVAR_ARCHIVE,
		"movement comfort mask: 0 disables it, 1 fully obscures the view edges" );
	vr_comfort_moving = gEngfuncs.Cvar_Get( "vr_comfort_moving", "0", 0,
		"internal VR movement state" );
	vr_scope_engaged = gEngfuncs.Cvar_Get( "vr_scope_engaged", "0", 0,
		"internal VR scope state" );
}

static void GL_OpenXRLogResult( const char *operation, XrResult result )
{
	char text[XR_MAX_RESULT_STRING_SIZE] = { 0 };

	if( xr.instance != XR_NULL_HANDLE && XR_FAILED( xrResultToString( xr.instance, result, text )))
		text[0] = '\0';

	if( text[0] )
		gEngfuncs.Con_Printf( S_ERROR "OpenXR: %s failed: %d (%s)\n", operation, (int)result, text );
	else
		gEngfuncs.Con_Printf( S_ERROR "OpenXR: %s failed: %d\n", operation, (int)result );
}

static qboolean GL_OpenXRCheck( XrResult result, const char *operation )
{
	if( XR_SUCCEEDED( result ))
		return true;

	GL_OpenXRLogResult( operation, result );
	return false;
}

static qboolean GL_OpenXRCreateAction( XrAction *action, XrActionType type, const char *name, const char *localized_name )
{
	XrActionCreateInfo create_info = { XR_TYPE_ACTION_CREATE_INFO };

	create_info.actionType = type;
	create_info.countSubactionPaths = REF_VR_MAX_HANDS;
	create_info.subactionPaths = xr.actions.hand_paths;
	Q_strncpy( create_info.actionName, name, sizeof( create_info.actionName ));
	Q_strncpy( create_info.localizedActionName, localized_name, sizeof( create_info.localizedActionName ));
	return GL_OpenXRCheck( xrCreateAction( xr.actions.gameplay_set, &create_info, action ), name );
}

static qboolean GL_OpenXRCreateActions( void )
{
	XrActionSetCreateInfo set_info = { XR_TYPE_ACTION_SET_CREATE_INFO };
	const char *hand_paths[REF_VR_MAX_HANDS] = { "/user/hand/left", "/user/hand/right" };

	Q_strncpy( set_info.actionSetName, "gameplay", sizeof( set_info.actionSetName ));
	Q_strncpy( set_info.localizedActionSetName, "Gameplay", sizeof( set_info.localizedActionSetName ));
	if( !GL_OpenXRCheck( xrCreateActionSet( xr.instance, &set_info, &xr.actions.gameplay_set ), "xrCreateActionSet(gameplay)" ))
		return false;

	for( int i = 0; i < REF_VR_MAX_HANDS; ++i )
	{
		if( !GL_OpenXRCheck( xrStringToPath( xr.instance, hand_paths[i], &xr.actions.hand_paths[i] ), "xrStringToPath(hand)" ))
			return false;
	}

	return GL_OpenXRCreateAction( &xr.actions.grip_pose, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose" ) &&
		GL_OpenXRCreateAction( &xr.actions.aim_pose, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose" ) &&
		GL_OpenXRCreateAction( &xr.actions.trigger, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger" ) &&
		GL_OpenXRCreateAction( &xr.actions.squeeze, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Squeeze" ) &&
		GL_OpenXRCreateAction( &xr.actions.stick, XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Stick" ) &&
		GL_OpenXRCreateAction( &xr.actions.primary, XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "Primary" ) &&
		GL_OpenXRCreateAction( &xr.actions.secondary, XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "Secondary" ) &&
		GL_OpenXRCreateAction( &xr.actions.menu, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu" ) &&
		GL_OpenXRCreateAction( &xr.actions.stick_click, XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click", "Stick Click" ) &&
		GL_OpenXRCreateAction( &xr.actions.haptic, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic" );
}

static void GL_OpenXRSuggestBindings( const char *profile_name, const gl_openxr_binding_t *bindings, uint32_t binding_count )
{
	XrActionSuggestedBinding suggested[32];
	XrInteractionProfileSuggestedBinding suggest_info = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	XrPath profile;
	uint32_t suggested_count = 0;

	if( !GL_OpenXRCheck( xrStringToPath( xr.instance, profile_name, &profile ), "xrStringToPath(profile)" ))
		return;

	for( uint32_t i = 0; i < binding_count; ++i )
	{
		for( int hand = 0; hand < REF_VR_MAX_HANDS; ++hand )
		{
			const char *path_name = hand == REF_VR_HAND_LEFT ? bindings[i].left_path : bindings[i].right_path;

			if( !path_name )
				continue;
			if( suggested_count >= ARRAYSIZE( suggested ))
				return;
			if( !GL_OpenXRCheck( xrStringToPath( xr.instance, path_name, &suggested[suggested_count].binding ),
				"xrStringToPath(binding)" ))
				continue;
			suggested[suggested_count].action = bindings[i].action;
			++suggested_count;
		}
	}

	if( !suggested_count )
		return;
	suggest_info.interactionProfile = profile;
	suggest_info.countSuggestedBindings = suggested_count;
	suggest_info.suggestedBindings = suggested;
	GL_OpenXRCheck( xrSuggestInteractionProfileBindings( xr.instance, &suggest_info ), "xrSuggestInteractionProfileBindings" );
}

static void GL_OpenXRSuggestInteractionProfiles( void )
{
	const gl_openxr_binding_t simple_bindings[] = {
		{ xr.actions.grip_pose, "/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose" },
		{ xr.actions.aim_pose, "/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose" },
		{ xr.actions.trigger, "/user/hand/left/input/select/click", "/user/hand/right/input/select/click" },
		{ xr.actions.menu, "/user/hand/left/input/menu/click", "/user/hand/right/input/menu/click" },
		{ xr.actions.haptic, "/user/hand/left/output/haptic", "/user/hand/right/output/haptic" },
	};
	const gl_openxr_binding_t touch_bindings[] = {
		{ xr.actions.grip_pose, "/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose" },
		{ xr.actions.aim_pose, "/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose" },
		{ xr.actions.trigger, "/user/hand/left/input/trigger/value", "/user/hand/right/input/trigger/value" },
		{ xr.actions.squeeze, "/user/hand/left/input/squeeze/value", "/user/hand/right/input/squeeze/value" },
		{ xr.actions.stick, "/user/hand/left/input/thumbstick", "/user/hand/right/input/thumbstick" },
		{ xr.actions.primary, "/user/hand/left/input/x/click", "/user/hand/right/input/a/click" },
		{ xr.actions.secondary, "/user/hand/left/input/y/click", "/user/hand/right/input/b/click" },
		{ xr.actions.menu, "/user/hand/left/input/menu/click", NULL },
		{ xr.actions.stick_click, "/user/hand/left/input/thumbstick/click", "/user/hand/right/input/thumbstick/click" },
		{ xr.actions.haptic, "/user/hand/left/output/haptic", "/user/hand/right/output/haptic" },
	};
	const gl_openxr_binding_t index_bindings[] = {
		{ xr.actions.grip_pose, "/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose" },
		{ xr.actions.aim_pose, "/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose" },
		{ xr.actions.trigger, "/user/hand/left/input/trigger/value", "/user/hand/right/input/trigger/value" },
		{ xr.actions.squeeze, "/user/hand/left/input/squeeze/value", "/user/hand/right/input/squeeze/value" },
		{ xr.actions.stick, "/user/hand/left/input/thumbstick", "/user/hand/right/input/thumbstick" },
		{ xr.actions.primary, "/user/hand/left/input/a/click", "/user/hand/right/input/a/click" },
		{ xr.actions.secondary, "/user/hand/left/input/b/click", "/user/hand/right/input/b/click" },
		{ xr.actions.menu, "/user/hand/left/input/system/click", "/user/hand/right/input/system/click" },
		{ xr.actions.stick_click, "/user/hand/left/input/thumbstick/click", "/user/hand/right/input/thumbstick/click" },
		{ xr.actions.haptic, "/user/hand/left/output/haptic", "/user/hand/right/output/haptic" },
	};
	const gl_openxr_binding_t motion_bindings[] = {
		{ xr.actions.grip_pose, "/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose" },
		{ xr.actions.aim_pose, "/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose" },
		{ xr.actions.trigger, "/user/hand/left/input/trigger/value", "/user/hand/right/input/trigger/value" },
		{ xr.actions.stick, "/user/hand/left/input/thumbstick", "/user/hand/right/input/thumbstick" },
		{ xr.actions.primary, "/user/hand/left/input/squeeze/click", "/user/hand/right/input/squeeze/click" },
		{ xr.actions.secondary, "/user/hand/left/input/trackpad/click", "/user/hand/right/input/trackpad/click" },
		{ xr.actions.menu, "/user/hand/left/input/menu/click", "/user/hand/right/input/menu/click" },
		{ xr.actions.stick_click, "/user/hand/left/input/thumbstick/click", "/user/hand/right/input/thumbstick/click" },
		{ xr.actions.haptic, "/user/hand/left/output/haptic", "/user/hand/right/output/haptic" },
	};
	const gl_openxr_binding_t vive_bindings[] = {
		{ xr.actions.grip_pose, "/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose" },
		{ xr.actions.aim_pose, "/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose" },
		{ xr.actions.trigger, "/user/hand/left/input/trigger/value", "/user/hand/right/input/trigger/value" },
		{ xr.actions.stick, "/user/hand/left/input/trackpad", "/user/hand/right/input/trackpad" },
		{ xr.actions.primary, "/user/hand/left/input/trigger/click", "/user/hand/right/input/trigger/click" },
		{ xr.actions.secondary, "/user/hand/left/input/squeeze/click", "/user/hand/right/input/squeeze/click" },
		{ xr.actions.menu, "/user/hand/left/input/menu/click", "/user/hand/right/input/menu/click" },
		{ xr.actions.stick_click, "/user/hand/left/input/trackpad/click", "/user/hand/right/input/trackpad/click" },
		{ xr.actions.haptic, "/user/hand/left/output/haptic", "/user/hand/right/output/haptic" },
	};

	GL_OpenXRSuggestBindings( "/interaction_profiles/khr/simple_controller", simple_bindings, ARRAYSIZE( simple_bindings ));
	GL_OpenXRSuggestBindings( "/interaction_profiles/oculus/touch_controller", touch_bindings, ARRAYSIZE( touch_bindings ));
	GL_OpenXRSuggestBindings( "/interaction_profiles/valve/index_controller", index_bindings, ARRAYSIZE( index_bindings ));
	GL_OpenXRSuggestBindings( "/interaction_profiles/microsoft/motion_controller", motion_bindings, ARRAYSIZE( motion_bindings ));
	GL_OpenXRSuggestBindings( "/interaction_profiles/htc/vive_controller", vive_bindings, ARRAYSIZE( vive_bindings ));
}

static qboolean GL_OpenXRCreateActionSpaces( void )
{
	XrActionSpaceCreateInfo space_info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };

	space_info.poseInActionSpace.orientation.w = 1.0f;
	for( int hand = 0; hand < REF_VR_MAX_HANDS; ++hand )
	{
		space_info.subactionPath = xr.actions.hand_paths[hand];
		space_info.action = xr.actions.grip_pose;
		if( !GL_OpenXRCheck( xrCreateActionSpace( xr.session, &space_info, &xr.actions.grip_spaces[hand] ),
			"xrCreateActionSpace(grip)" ))
			return false;
		space_info.action = xr.actions.aim_pose;
		if( !GL_OpenXRCheck( xrCreateActionSpace( xr.session, &space_info, &xr.actions.aim_spaces[hand] ),
			"xrCreateActionSpace(aim)" ))
			return false;
	}
	return true;
}

static qboolean GL_OpenXRLoadGLFunctions( void )
{
	if( !pglGenFramebuffers ) pglGenFramebuffers = gEngfuncs.GL_GetProcAddress( "glGenFramebuffers" );
	if( !pglDeleteFramebuffers ) pglDeleteFramebuffers = gEngfuncs.GL_GetProcAddress( "glDeleteFramebuffers" );
	if( !pglBindFramebuffer ) pglBindFramebuffer = gEngfuncs.GL_GetProcAddress( "glBindFramebuffer" );
	if( !pglFramebufferTexture2D ) pglFramebufferTexture2D = gEngfuncs.GL_GetProcAddress( "glFramebufferTexture2D" );
	if( !pglCheckFramebufferStatus ) pglCheckFramebufferStatus = gEngfuncs.GL_GetProcAddress( "glCheckFramebufferStatus" );
	if( !pglBlitFramebuffer ) pglBlitFramebuffer = gEngfuncs.GL_GetProcAddress( "glBlitFramebuffer" );

	if( pglGenFramebuffers && pglDeleteFramebuffers && pglBindFramebuffer &&
		pglFramebufferTexture2D && pglCheckFramebufferStatus && pglBlitFramebuffer )
		return true;

	gEngfuncs.Con_Printf( S_ERROR "OpenXR: required framebuffer functions are unavailable\n" );
	return false;
}

static XrQuaternionf GL_OpenXRQuatConjugate( XrQuaternionf q )
{
	XrQuaternionf out = { -q.x, -q.y, -q.z, q.w };
	return out;
}

static XrQuaternionf GL_OpenXRQuatMultiply( XrQuaternionf a, XrQuaternionf b )
{
	XrQuaternionf out = {
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
	};
	return out;
}

static XrVector3f GL_OpenXRRotateVector( XrQuaternionf q, XrVector3f v )
{
	XrQuaternionf p = { v.x, v.y, v.z, 0.0f };
	XrQuaternionf rotated = GL_OpenXRQuatMultiply( GL_OpenXRQuatMultiply( q, p ), GL_OpenXRQuatConjugate( q ));
	XrVector3f out = { rotated.x, rotated.y, rotated.z };
	return out;
}

static XrPosef GL_OpenXRPoseInverse( XrPosef pose )
{
	XrPosef out;
	out.orientation = GL_OpenXRQuatConjugate( pose.orientation );
	XrVector3f negative = { -pose.position.x, -pose.position.y, -pose.position.z };
	out.position = GL_OpenXRRotateVector( out.orientation, negative );
	return out;
}

static XrPosef GL_OpenXRPoseMultiply( XrPosef a, XrPosef b )
{
	XrPosef out;
	XrVector3f position = GL_OpenXRRotateVector( a.orientation, b.position );
	out.orientation = GL_OpenXRQuatMultiply( a.orientation, b.orientation );
	out.position.x = a.position.x + position.x;
	out.position.y = a.position.y + position.y;
	out.position.z = a.position.z + position.z;
	return out;
}

static void GL_OpenXRMapVector( const XrVector3f *in, vec3_t out )
{
	// OpenXR right/up/back maps to Xash forward/left/up.
	out[0] = -in->z;
	out[1] = -in->x;
	out[2] = in->y;
}

static void GL_OpenXRQuatToBasis( XrQuaternionf q, vec3_t forward, vec3_t right, vec3_t up )
{
	const XrVector3f xr_forward = { 0.0f, 0.0f, -1.0f };
	const XrVector3f xr_right = { 1.0f, 0.0f, 0.0f };
	const XrVector3f xr_up = { 0.0f, 1.0f, 0.0f };
	XrVector3f rotated_forward = GL_OpenXRRotateVector( q, xr_forward );
	XrVector3f rotated_right = GL_OpenXRRotateVector( q, xr_right );
	XrVector3f rotated_up = GL_OpenXRRotateVector( q, xr_up );

	GL_OpenXRMapVector( &rotated_forward, forward );
	GL_OpenXRMapVector( &rotated_right, right );
	GL_OpenXRMapVector( &rotated_up, up );
	VectorNormalize( forward );
	VectorNormalize( right );
	VectorNormalize( up );
}

static void GL_OpenXRFillPose( const XrPosef *pose, ref_vr_pose_t *out )
{
	GL_OpenXRMapVector( &pose->position, out->position );
	GL_OpenXRQuatToBasis( pose->orientation, out->forward, out->right, out->up );
}

static qboolean GL_OpenXRGetBooleanAction( XrAction action, XrPath hand_path, qboolean *value )
{
	XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };

	get_info.action = action;
	get_info.subactionPath = hand_path;
	if( !GL_OpenXRCheck( xrGetActionStateBoolean( xr.session, &get_info, &state ), "xrGetActionStateBoolean" ))
		return false;
	*value = state.isActive && state.currentState;
	return true;
}

static qboolean GL_OpenXRGetFloatAction( XrAction action, XrPath hand_path, float *value )
{
	XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };

	get_info.action = action;
	get_info.subactionPath = hand_path;
	if( !GL_OpenXRCheck( xrGetActionStateFloat( xr.session, &get_info, &state ), "xrGetActionStateFloat" ))
		return false;
	*value = state.isActive ? state.currentState : 0.0f;
	return true;
}

static qboolean GL_OpenXRGetVector2Action( XrAction action, XrPath hand_path, float value[2] )
{
	XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateVector2f state = { XR_TYPE_ACTION_STATE_VECTOR2F };

	get_info.action = action;
	get_info.subactionPath = hand_path;
	if( !GL_OpenXRCheck( xrGetActionStateVector2f( xr.session, &get_info, &state ), "xrGetActionStateVector2f" ))
		return false;
	value[0] = state.isActive ? state.currentState.x : 0.0f;
	value[1] = state.isActive ? state.currentState.y : 0.0f;
	return true;
}

static void GL_OpenXRFillUICursor( const XrPosef *aim, ref_vr_hand_t *out )
{
	XrVector3f world_origin;
	XrVector3f aim_forward = { 0.0f, 0.0f, -1.0f };
	XrVector3f world_direction;
	XrVector3f local_origin;
	XrVector3f local_direction;
	float distance = bound( 0.2f, vr_ui_distance->value, 10.0f );
	float width = bound( 0.25f, vr_ui_width->value, 8.0f );
	float height = width * (float)xr.ui.height / (float)xr.ui.width;
	float t, x, y;

	world_origin.x = aim->position.x - xr.head_pose.position.x;
	world_origin.y = aim->position.y - xr.head_pose.position.y;
	world_origin.z = aim->position.z - xr.head_pose.position.z;
	world_direction = GL_OpenXRRotateVector( aim->orientation, aim_forward );
	local_origin = GL_OpenXRRotateVector( GL_OpenXRQuatConjugate( xr.head_pose.orientation ), world_origin );
	local_direction = GL_OpenXRRotateVector( GL_OpenXRQuatConjugate( xr.head_pose.orientation ), world_direction );

	if( local_direction.z >= -0.0001f )
		return;
	t = ( -distance - local_origin.z ) / local_direction.z;
	if( t <= 0.0f )
		return;
	x = local_origin.x + local_direction.x * t;
	y = local_origin.y + local_direction.y * t;
	out->ui_cursor[0] = x / width + 0.5f;
	out->ui_cursor[1] = 0.5f - y / height;
	if( out->ui_cursor[0] >= 0.0f && out->ui_cursor[0] <= 1.0f &&
		out->ui_cursor[1] >= 0.0f && out->ui_cursor[1] <= 1.0f )
		SetBits( out->flags, REF_VR_HAND_UI_VALID );
}

static qboolean GL_OpenXRFillHandPose( int hand, XrAction action, XrSpace space, uint32_t flag, qboolean include_velocity,
	ref_vr_hand_t *out )
{
	XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStatePose state = { XR_TYPE_ACTION_STATE_POSE };
	XrSpaceVelocity velocity = { XR_TYPE_SPACE_VELOCITY };
	XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
	const XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;

	get_info.action = action;
	get_info.subactionPath = xr.actions.hand_paths[hand];
	if( !GL_OpenXRCheck( xrGetActionStatePose( xr.session, &get_info, &state ), "xrGetActionStatePose" ))
		return false;
	if( !state.isActive )
		return true;
	if( include_velocity )
		location.next = &velocity;
	if( !GL_OpenXRCheck( xrLocateSpace( space, xr.local_space, xr.frame_state.predictedDisplayTime, &location ),
		"xrLocateSpace(controller)" ) || ( location.locationFlags & required ) != required )
		return false;

	GL_OpenXRFillPose( &location.pose, flag == REF_VR_HAND_GRIP_VALID ? &out->grip : &out->aim );
	SetBits( out->flags, flag );
	if( flag == REF_VR_HAND_AIM_VALID && xr.views_valid )
		GL_OpenXRFillUICursor( &location.pose, out );
	if( include_velocity )
	{
		if( velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT )
			GL_OpenXRMapVector( &velocity.linearVelocity, out->linear_velocity );
		if( velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT )
			GL_OpenXRMapVector( &velocity.angularVelocity, out->angular_velocity );
	}
	return true;
}

static qboolean GL_OpenXRFillHands( ref_vr_hand_t hands[REF_VR_MAX_HANDS] )
{
	qboolean valid = true;

	for( int hand = 0; hand < REF_VR_MAX_HANDS; ++hand )
	{
		qboolean pressed;

		valid = GL_OpenXRFillHandPose( hand, xr.actions.grip_pose, xr.actions.grip_spaces[hand],
			REF_VR_HAND_GRIP_VALID, true, &hands[hand] ) && valid;
		valid = GL_OpenXRFillHandPose( hand, xr.actions.aim_pose, xr.actions.aim_spaces[hand],
			REF_VR_HAND_AIM_VALID, false, &hands[hand] ) && valid;
		valid = GL_OpenXRGetFloatAction( xr.actions.trigger, xr.actions.hand_paths[hand], &hands[hand].trigger ) && valid;
		valid = GL_OpenXRGetFloatAction( xr.actions.squeeze, xr.actions.hand_paths[hand], &hands[hand].squeeze ) && valid;
		valid = GL_OpenXRGetVector2Action( xr.actions.stick, xr.actions.hand_paths[hand], hands[hand].stick ) && valid;
		if( GL_OpenXRGetBooleanAction( xr.actions.primary, xr.actions.hand_paths[hand], &pressed ))
		{
			if( pressed ) SetBits( hands[hand].buttons, REF_VR_BUTTON_PRIMARY );
		}
		else valid = false;
		if( GL_OpenXRGetBooleanAction( xr.actions.secondary, xr.actions.hand_paths[hand], &pressed ))
		{
			if( pressed ) SetBits( hands[hand].buttons, REF_VR_BUTTON_SECONDARY );
		}
		else valid = false;
		if( GL_OpenXRGetBooleanAction( xr.actions.menu, xr.actions.hand_paths[hand], &pressed ))
		{
			if( pressed ) SetBits( hands[hand].buttons, REF_VR_BUTTON_MENU );
		}
		else valid = false;
		if( GL_OpenXRGetBooleanAction( xr.actions.stick_click, xr.actions.hand_paths[hand], &pressed ))
		{
			if( pressed ) SetBits( hands[hand].buttons, REF_VR_BUTTON_STICK );
		}
		else valid = false;
	}
	return valid;
}

static qboolean GL_OpenXRSyncActions( void )
{
	XrActiveActionSet active_set = { xr.actions.gameplay_set, XR_NULL_PATH };
	XrActionsSyncInfo sync_info = { XR_TYPE_ACTIONS_SYNC_INFO };

	sync_info.countActiveActionSets = 1;
	sync_info.activeActionSets = &active_set;
	return GL_OpenXRCheck( xrSyncActions( xr.session, &sync_info ), "xrSyncActions" );
}

static void GL_OpenXRDestroyEye( gl_openxr_eye_t *eye )
{
	if( eye->acquired && eye->waited && eye->swapchain != XR_NULL_HANDLE )
	{
		XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		GL_OpenXRCheck( xrReleaseSwapchainImage( eye->swapchain, &release_info ), "xrReleaseSwapchainImage(shutdown)" );
	}

	if( eye->framebuffers )
		pglDeleteFramebuffers( eye->image_count, eye->framebuffers );
	if( eye->swapchain != XR_NULL_HANDLE )
		GL_OpenXRCheck( xrDestroySwapchain( eye->swapchain ), "xrDestroySwapchain" );

	free( eye->framebuffers );
	free( eye->images );
	memset( eye, 0, sizeof( *eye ));
}

void GL_OpenXRShutdown( void )
{
	if( xr.ui_active && xr.initialized )
	{
		pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
		pglViewport( 0, 0, xr.ui_source_width, xr.ui_source_height );
		pglClearColor( xr.ui_clear_color[0], xr.ui_clear_color[1], xr.ui_clear_color[2], xr.ui_clear_color[3] );
		xr.ui_active = false;
	}

	/* OpenXR requires every waited swapchain image to be released before its
	 * frame/session is ended. This also unwinds an interrupted eye/UI pass. */
	for( int i = 0; i <= REF_VR_MAX_EYES; ++i )
	{
		gl_openxr_eye_t *eye = i == REF_VR_MAX_EYES ? &xr.ui : &xr.eyes[i];
		if( eye->acquired && eye->waited && eye->swapchain != XR_NULL_HANDLE )
		{
			XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			GL_OpenXRCheck( xrReleaseSwapchainImage( eye->swapchain, &release_info ),
				"xrReleaseSwapchainImage(shutdown)" );
			eye->acquired = false;
			eye->waited = false;
		}
	}

	if( xr.frame_begun && xr.session != XR_NULL_HANDLE )
	{
		XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
		end_info.displayTime = xr.frame_state.predictedDisplayTime;
		end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		GL_OpenXRCheck( xrEndFrame( xr.session, &end_info ), "xrEndFrame(shutdown)" );
	}

	if( xr.initialized )
		pglFinish();

	for( int i = 0; i < REF_VR_MAX_EYES; ++i )
		GL_OpenXRDestroyEye( &xr.eyes[i] );
	GL_OpenXRDestroyEye( &xr.ui );

	for( int i = 0; i < REF_VR_MAX_HANDS; ++i )
	{
		if( xr.actions.aim_spaces[i] != XR_NULL_HANDLE )
			GL_OpenXRCheck( xrDestroySpace( xr.actions.aim_spaces[i] ), "xrDestroySpace(aim)" );
		if( xr.actions.grip_spaces[i] != XR_NULL_HANDLE )
			GL_OpenXRCheck( xrDestroySpace( xr.actions.grip_spaces[i] ), "xrDestroySpace(grip)" );
	}
	if( xr.head_space != XR_NULL_HANDLE )
		GL_OpenXRCheck( xrDestroySpace( xr.head_space ), "xrDestroySpace(head)" );
	if( xr.local_space != XR_NULL_HANDLE )
		GL_OpenXRCheck( xrDestroySpace( xr.local_space ), "xrDestroySpace(local)" );
	if( xr.session != XR_NULL_HANDLE )
		GL_OpenXRCheck( xrDestroySession( xr.session ), "xrDestroySession" );
	if( xr.actions.gameplay_set != XR_NULL_HANDLE )
		GL_OpenXRCheck( xrDestroyActionSet( xr.actions.gameplay_set ), "xrDestroyActionSet(gameplay)" );
	if( xr.instance != XR_NULL_HANDLE )
		GL_OpenXRCheck( xrDestroyInstance( xr.instance ), "xrDestroyInstance" );

	memset( &xr, 0, sizeof( xr ));
	xr.current_eye = -1;
}

static qboolean GL_OpenXRCreateEye( gl_openxr_eye_t *eye, const XrViewConfigurationView *view, int64_t format )
{
	float scale = bound( 0.5f, vr_supersampling->value, 2.0f );
	XrSwapchainCreateInfo create_info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
	uint32_t image_count = 0;

	eye->width = (uint32_t)( view->recommendedImageRectWidth * scale );
	eye->height = (uint32_t)( view->recommendedImageRectHeight * scale );
	create_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
	create_info.format = format;
	create_info.sampleCount = 1;
	create_info.width = eye->width;
	create_info.height = eye->height;
	create_info.faceCount = 1;
	create_info.arraySize = 1;
	create_info.mipCount = 1;

	if( !GL_OpenXRCheck( xrCreateSwapchain( xr.session, &create_info, &eye->swapchain ), "xrCreateSwapchain" ))
		return false;
	if( !GL_OpenXRCheck( xrEnumerateSwapchainImages( eye->swapchain, 0, &image_count, NULL ), "xrEnumerateSwapchainImages(count)" ))
		return false;

	eye->images = calloc( image_count, sizeof( *eye->images ));
	eye->framebuffers = calloc( image_count, sizeof( *eye->framebuffers ));
	if( !eye->images || !eye->framebuffers )
		return false;

	eye->image_count = image_count;
	for( uint32_t i = 0; i < image_count; ++i )
		eye->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;

	if( !GL_OpenXRCheck( xrEnumerateSwapchainImages( eye->swapchain, image_count, &image_count,
		(XrSwapchainImageBaseHeader *)eye->images ), "xrEnumerateSwapchainImages" ))
		return false;

	pglGenFramebuffers( image_count, eye->framebuffers );
	for( uint32_t i = 0; i < image_count; ++i )
	{
		pglBindFramebuffer( GL_FRAMEBUFFER, eye->framebuffers[i] );
		pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, eye->images[i].image, 0 );
		if( pglCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
		{
			gEngfuncs.Con_Printf( S_ERROR "OpenXR: incomplete eye framebuffer %u\n", i );
			pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
			return false;
		}
	}
	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	return true;
}

static qboolean GL_OpenXRInitialize( void )
{
	const char *extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
	XrInstanceCreateInfo instance_info = { XR_TYPE_INSTANCE_CREATE_INFO };
	XrSystemGetInfo system_info = { XR_TYPE_SYSTEM_GET_INFO };
	PFN_xrGetOpenGLGraphicsRequirementsKHR get_requirements = NULL;
	XrGraphicsRequirementsOpenGLKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	XrGraphicsBindingOpenGLXlibKHR binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR };
	XrSessionCreateInfo session_info = { XR_TYPE_SESSION_CREATE_INFO };
	XrReferenceSpaceCreateInfo space_info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	XrViewConfigurationView view_configs[REF_VR_MAX_EYES];
	uint32_t view_count = 0;
	uint32_t format_count = 0;
	int64_t *formats = NULL;
	int64_t selected_format = 0;
	int fbconfig_id = 0;
	int config_count = 0;
	GLXFBConfig *configs = NULL;
	XVisualInfo *visual = NULL;
	qboolean success = false;

	memset( &xr, 0, sizeof( xr ));
	xr.current_eye = -1;

	Q_strncpy( instance_info.applicationInfo.applicationName, "Xash3D FWGS VR", XR_MAX_APPLICATION_NAME_SIZE );
	instance_info.applicationInfo.applicationVersion = 1;
	Q_strncpy( instance_info.applicationInfo.engineName, "Xash3D FWGS", XR_MAX_ENGINE_NAME_SIZE );
	instance_info.applicationInfo.engineVersion = 1;
	instance_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	instance_info.enabledExtensionCount = ARRAYSIZE( extensions );
	instance_info.enabledExtensionNames = extensions;

	if( !GL_OpenXRCheck( xrCreateInstance( &instance_info, &xr.instance ), "xrCreateInstance" ))
		goto cleanup;
	if( !GL_OpenXRCreateActions() )
		goto cleanup;
	GL_OpenXRSuggestInteractionProfiles();

	system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if( !GL_OpenXRCheck( xrGetSystem( xr.instance, &system_info, &xr.system_id ), "xrGetSystem" ))
		goto cleanup;
	if( !GL_OpenXRCheck( xrGetInstanceProcAddr( xr.instance, "xrGetOpenGLGraphicsRequirementsKHR",
		(PFN_xrVoidFunction *)&get_requirements ), "xrGetInstanceProcAddr(graphics requirements)" ) || !get_requirements )
		goto cleanup;
	if( !GL_OpenXRCheck( get_requirements( xr.instance, xr.system_id, &requirements ), "xrGetOpenGLGraphicsRequirementsKHR" ))
		goto cleanup;
	if( !GL_OpenXRLoadGLFunctions() )
		goto cleanup;

	binding.xDisplay = glXGetCurrentDisplay();
	binding.glxContext = glXGetCurrentContext();
	binding.glxDrawable = glXGetCurrentDrawable();
	if( !binding.xDisplay || !binding.glxContext || !binding.glxDrawable )
	{
		gEngfuncs.Con_Printf( S_ERROR "OpenXR: a current GLX display, context, and drawable are required\n" );
		goto cleanup;
	}
	if( glXQueryContext( binding.xDisplay, binding.glxContext, GLX_FBCONFIG_ID, &fbconfig_id ) != Success )
	{
		gEngfuncs.Con_Printf( S_ERROR "OpenXR: failed to query the current GLX framebuffer configuration\n" );
		goto cleanup;
	}
	{
		int attributes[] = { GLX_FBCONFIG_ID, fbconfig_id, None };
		configs = glXChooseFBConfig( binding.xDisplay, DefaultScreen( binding.xDisplay ), attributes, &config_count );
	}
	if( !configs || config_count < 1 )
		goto cleanup;
	binding.glxFBConfig = configs[0];
	visual = glXGetVisualFromFBConfig( binding.xDisplay, binding.glxFBConfig );
	if( !visual )
		goto cleanup;
	binding.visualid = visual->visualid;

	session_info.next = &binding;
	session_info.systemId = xr.system_id;
	if( !GL_OpenXRCheck( xrCreateSession( xr.instance, &session_info, &xr.session ), "xrCreateSession" ))
		goto cleanup;
	{
		XrSessionActionSetsAttachInfo attach_info = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
		attach_info.countActionSets = 1;
		attach_info.actionSets = &xr.actions.gameplay_set;
		if( !GL_OpenXRCheck( xrAttachSessionActionSets( xr.session, &attach_info ), "xrAttachSessionActionSets" ))
			goto cleanup;
	}
	if( !GL_OpenXRCreateActionSpaces() )
		goto cleanup;

	space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	space_info.poseInReferenceSpace.orientation.w = 1.0f;
	if( !GL_OpenXRCheck( xrCreateReferenceSpace( xr.session, &space_info, &xr.local_space ), "xrCreateReferenceSpace(local)" ))
		goto cleanup;
	space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if( !GL_OpenXRCheck( xrCreateReferenceSpace( xr.session, &space_info, &xr.head_space ), "xrCreateReferenceSpace(view)" ))
		goto cleanup;

	memset( view_configs, 0, sizeof( view_configs ));
	for( int i = 0; i < REF_VR_MAX_EYES; ++i )
		view_configs[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	if( !GL_OpenXRCheck( xrEnumerateViewConfigurationViews( xr.instance, xr.system_id,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, REF_VR_MAX_EYES, &view_count, view_configs ),
		"xrEnumerateViewConfigurationViews" ) || view_count != REF_VR_MAX_EYES )
		goto cleanup;

	if( !GL_OpenXRCheck( xrEnumerateSwapchainFormats( xr.session, 0, &format_count, NULL ), "xrEnumerateSwapchainFormats(count)" ))
		goto cleanup;
	formats = calloc( format_count, sizeof( *formats ));
	if( !formats )
		goto cleanup;
	if( !GL_OpenXRCheck( xrEnumerateSwapchainFormats( xr.session, format_count, &format_count, formats ), "xrEnumerateSwapchainFormats" ))
		goto cleanup;
	for( uint32_t i = 0; i < format_count; ++i )
	{
		if( formats[i] == GL_SRGB8_ALPHA8 )
		{
			selected_format = formats[i];
			break;
		}
		if( !selected_format && formats[i] == GL_RGBA8 )
			selected_format = formats[i];
	}
	if( !selected_format )
	{
		gEngfuncs.Con_Printf( S_ERROR "OpenXR: runtime exposes no usable RGBA8 swapchain format\n" );
		goto cleanup;
	}

	for( int i = 0; i < REF_VR_MAX_EYES; ++i )
	{
		xr.views[i].type = XR_TYPE_VIEW;
		if( !GL_OpenXRCreateEye( &xr.eyes[i], &view_configs[i], selected_format ))
			goto cleanup;
	}
	{
		XrViewConfigurationView ui_config = { XR_TYPE_VIEW_CONFIGURATION_VIEW };
		ui_config.recommendedImageRectWidth = 1280;
		ui_config.recommendedImageRectHeight = 720;
		if( !GL_OpenXRCreateEye( &xr.ui, &ui_config, selected_format ))
			goto cleanup;
	}

	xr.initialized = true;
	gEngfuncs.Con_Printf( "OpenXR: initialized Linux/GLX stereo session (%ux%u per eye)\n",
		xr.eyes[0].width, xr.eyes[0].height );
	success = true;

cleanup:
	if( visual ) XFree( visual );
	if( configs ) XFree( configs );
	free( formats );
	if( !success )
	{
		GL_OpenXRShutdown();
		xr.failed = true;
	}
	return success;
}

static void GL_OpenXRPollEvents( void )
{
	XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
	XrResult result;

	while(( result = xrPollEvent( xr.instance, &event )) == XR_SUCCESS )
	{
		if( event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING )
			xr.exit_requested = true;
		else if( event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING )
			xr.reference_changed = true;
		else if( event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED )
		{
			const XrEventDataSessionStateChanged *changed = (const XrEventDataSessionStateChanged *)&event;
			xr.session_state = changed->state;
			switch( changed->state )
			{
			case XR_SESSION_STATE_READY:
				if( !xr.session_running )
				{
					XrSessionBeginInfo begin_info = { XR_TYPE_SESSION_BEGIN_INFO };
					begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
					if( GL_OpenXRCheck( xrBeginSession( xr.session, &begin_info ), "xrBeginSession" ))
						xr.session_running = true;
				}
				break;
			case XR_SESSION_STATE_STOPPING:
				if( xr.session_running )
				{
					GL_OpenXRCheck( xrEndSession( xr.session ), "xrEndSession" );
					xr.session_running = false;
				}
				break;
			case XR_SESSION_STATE_FOCUSED:
				xr.focused = true;
				break;
			case XR_SESSION_STATE_VISIBLE:
			case XR_SESSION_STATE_SYNCHRONIZED:
				xr.focused = false;
				break;
			case XR_SESSION_STATE_EXITING:
			case XR_SESSION_STATE_LOSS_PENDING:
				xr.exit_requested = true;
				xr.session_running = false;
				break;
			default:
				break;
			}
		}

		memset( &event, 0, sizeof( event ));
		event.type = XR_TYPE_EVENT_DATA_BUFFER;
	}

	if( result != XR_EVENT_UNAVAILABLE )
		GL_OpenXRCheck( result, "xrPollEvent" );
}

qboolean GL_OpenXRFrameBegin( ref_vr_frame_t *frame )
{
	XrFrameWaitInfo wait_info = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameBeginInfo begin_info = { XR_TYPE_FRAME_BEGIN_INFO };
	XrViewLocateInfo locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
	XrViewState view_state = { XR_TYPE_VIEW_STATE };
	XrSpaceLocation head_location = { XR_TYPE_SPACE_LOCATION };
	uint32_t view_count = 0;
	XrResult result;
	qboolean actions_synced;

	if( xr.frame_begun )
		GL_OpenXRFrameEnd();

	if( !vr_enable )
		GL_OpenXRInit();

	if( frame )
	{
		memset( frame, 0, sizeof( *frame ));
		frame->version = REF_VR_FRAME_VERSION;
		frame->struct_size = sizeof( *frame );
	}

	if( !vr_enable->value )
	{
		if( xr.initialized ) GL_OpenXRShutdown();
		xr.failed = false;
		return false;
	}
	if( xr.failed || ( !xr.initialized && !GL_OpenXRInitialize() ))
		return false;

	GL_OpenXRPollEvents();
	if( xr.exit_requested )
	{
		gEngfuncs.Con_Printf( "OpenXR: runtime requested session shutdown; toggle vr_enable to retry\n" );
		GL_OpenXRShutdown();
		xr.failed = true;
		return false;
	}
	if( !xr.session_running )
		return false;

	memset( &xr.frame_state, 0, sizeof( xr.frame_state ));
	xr.frame_state.type = XR_TYPE_FRAME_STATE;
	if( !GL_OpenXRCheck( xrWaitFrame( xr.session, &wait_info, &xr.frame_state ), "xrWaitFrame" ))
		return false;
	result = xrBeginFrame( xr.session, &begin_info );
	if( !GL_OpenXRCheck( result, "xrBeginFrame" ))
		return false;
	if( result == XR_FRAME_DISCARDED )
		xr.frame_state.shouldRender = XR_FALSE;

	xr.frame_begun = true;
	xr.views_valid = false;
	xr.current_eye = -1;
	xr.ui_active = false;
	xr.ui.ready = false;
	for( int i = 0; i < REF_VR_MAX_EYES; ++i )
		xr.eyes[i].ready = false;
	actions_synced = GL_OpenXRSyncActions();

	locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locate_info.displayTime = xr.frame_state.predictedDisplayTime;
	locate_info.space = xr.local_space;
	result = xrLocateViews( xr.session, &locate_info, &view_state, REF_VR_MAX_EYES, &view_count, xr.views );
	if( GL_OpenXRCheck( result, "xrLocateViews" ) && view_count == REF_VR_MAX_EYES )
	{
		const XrViewStateFlags required = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
		xr.views_valid = ( view_state.viewStateFlags & required ) == required;
	}

	result = xrLocateSpace( xr.head_space, xr.local_space, xr.frame_state.predictedDisplayTime, &head_location );
	if( GL_OpenXRCheck( result, "xrLocateSpace(head)" ))
	{
		const XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
		if(( head_location.locationFlags & required ) == required )
			xr.head_pose = head_location.pose;
		else xr.views_valid = false;
	}
	else xr.views_valid = false;

	if( frame )
	{
		frame->frame_id = ++xr.frame_id;
		frame->flags = REF_VR_FRAME_SESSION_ACTIVE;
		if( xr.focused ) SetBits( frame->flags, REF_VR_FRAME_FOCUSED );
		if( actions_synced )
			actions_synced = GL_OpenXRFillHands( frame->hands );
		if( actions_synced ) SetBits( frame->flags, REF_VR_FRAME_ACTIONS_VALID );
		if( xr.reference_changed ) SetBits( frame->flags, REF_VR_FRAME_REFERENCE_CHANGED );
		if( xr.views_valid )
		{
			SetBits( frame->flags, REF_VR_FRAME_HEAD_VALID );
			GL_OpenXRFillPose( &xr.head_pose, &frame->head );
			frame->eye_count = REF_VR_MAX_EYES;
		}
		if( xr.frame_state.shouldRender && xr.views_valid )
			SetBits( frame->flags, REF_VR_FRAME_SHOULD_RENDER );
	}
	xr.reference_changed = false;

	return true;
}

qboolean GL_OpenXRBeginEye( int eye )
{
	if( !xr.frame_begun || !xr.frame_state.shouldRender || !xr.views_valid || eye < 0 || eye >= REF_VR_MAX_EYES )
		return false;

	xr.current_eye = eye;
	return true;
}

qboolean GL_OpenXREndEye( int eye, int source_width, int source_height )
{
	gl_openxr_eye_t *target;
	XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };

	if( eye != xr.current_eye || eye < 0 || eye >= REF_VR_MAX_EYES )
		return false;
	target = &xr.eyes[eye];
	xr.current_eye = -1;

	if( source_width <= 0 || source_height <= 0 )
		return false;
	if( !GL_OpenXRCheck( xrAcquireSwapchainImage( target->swapchain, &acquire_info, &target->image_index ), "xrAcquireSwapchainImage" ))
		return false;
	target->acquired = true;
	target->waited = false;
	wait_info.timeout = XR_INFINITE_DURATION;
	if( !GL_OpenXRCheck( xrWaitSwapchainImage( target->swapchain, &wait_info ), "xrWaitSwapchainImage" ))
	{
		GL_OpenXRShutdown();
		xr.failed = true;
		return false;
	}
	target->waited = true;

	pglBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
	pglReadBuffer( GL_BACK );
	pglBindFramebuffer( GL_DRAW_FRAMEBUFFER, target->framebuffers[target->image_index] );
	pglBlitFramebuffer( 0, 0, source_width, source_height, 0, 0, target->width, target->height,
		GL_COLOR_BUFFER_BIT, GL_LINEAR );
	pglBindFramebuffer( GL_FRAMEBUFFER, target->framebuffers[target->image_index] );
	pglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE );
	pglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	pglClear( GL_COLOR_BUFFER_BIT );
	pglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	{
		GLboolean scissor_enabled = pglIsEnabled( GL_SCISSOR_TEST );
		GLint old_scissor[4];
		GLfloat old_clear[4];
		float target_level = vr_scope_engaged->value ? 0.75f :
			( vr_comfort_moving->value ? bound( 0.0f, vr_comfort_mask->value, 1.0f ) : 0.0f );
		float step_source = vr_comfort_mask->value > 0.0f ?
			bound( 0.0f, vr_comfort_mask->value, 1.0f ) : 0.75f;

		if( xr.comfort_frame != xr.frame_id )
		{
			float step = step_source * 0.05f;
			xr.comfort_frame = xr.frame_id;
			if( xr.comfort_level < target_level )
				xr.comfort_level = Q_min( target_level, xr.comfort_level + step );
			else if( xr.comfort_level > target_level )
				xr.comfort_level = Q_max( target_level, xr.comfort_level - step );
		}

		if( xr.comfort_level > 0.0f )
		{
			GLint mask_width = (GLint)( target->width * 0.5f * xr.comfort_level );
			GLint mask_height = (GLint)( target->height * 0.5f * xr.comfort_level );

			pglGetIntegerv( GL_SCISSOR_BOX, old_scissor );
			pglGetFloatv( GL_COLOR_CLEAR_VALUE, old_clear );
			pglEnable( GL_SCISSOR_TEST );
			pglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
			pglScissor( 0, 0, target->width, mask_height );
			pglClear( GL_COLOR_BUFFER_BIT );
			pglScissor( 0, target->height - mask_height, target->width, mask_height );
			pglClear( GL_COLOR_BUFFER_BIT );
			pglScissor( 0, 0, mask_width, target->height );
			pglClear( GL_COLOR_BUFFER_BIT );
			pglScissor( target->width - mask_width, 0, mask_width, target->height );
			pglClear( GL_COLOR_BUFFER_BIT );
			pglScissor( old_scissor[0], old_scissor[1], old_scissor[2], old_scissor[3] );
			if( !scissor_enabled ) pglDisable( GL_SCISSOR_TEST );
			pglClearColor( old_clear[0], old_clear[1], old_clear[2], old_clear[3] );
		}
	}
	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	pglViewport( 0, 0, source_width, source_height );

	if( !GL_OpenXRCheck( xrReleaseSwapchainImage( target->swapchain, &release_info ), "xrReleaseSwapchainImage" ))
	{
		GL_OpenXRShutdown();
		xr.failed = true;
		return false;
	}
	target->acquired = false;
	target->waited = false;
	target->ready = true;
	return true;
}

qboolean GL_OpenXRBeginUI( int width, int height )
{
	XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };

	if( !xr.frame_begun || !xr.frame_state.shouldRender || !xr.views_valid || xr.ui_active ||
		width <= 0 || height <= 0 || xr.ui.swapchain == XR_NULL_HANDLE )
		return false;

	if( !GL_OpenXRCheck( xrAcquireSwapchainImage( xr.ui.swapchain, &acquire_info, &xr.ui.image_index ),
		"xrAcquireSwapchainImage(UI)" ))
		return false;
	xr.ui.acquired = true;
	wait_info.timeout = XR_INFINITE_DURATION;
	if( !GL_OpenXRCheck( xrWaitSwapchainImage( xr.ui.swapchain, &wait_info ), "xrWaitSwapchainImage(UI)" ))
	{
		GL_OpenXRShutdown();
		xr.failed = true;
		return false;
	}
	xr.ui.waited = true;
	xr.ui_active = true;
	xr.ui_source_width = width;
	xr.ui_source_height = height;
	pglGetFloatv( GL_COLOR_CLEAR_VALUE, xr.ui_clear_color );
	pglBindFramebuffer( GL_FRAMEBUFFER, xr.ui.framebuffers[xr.ui.image_index] );
	pglViewport( 0, 0, xr.ui.width, xr.ui.height );
	pglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	pglClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	pglClear( GL_COLOR_BUFFER_BIT );
	return true;
}

void GL_OpenXREndUI( void )
{
	XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };

	if( !xr.ui_active )
		return;

	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	pglViewport( 0, 0, xr.ui_source_width, xr.ui_source_height );
	pglClearColor( xr.ui_clear_color[0], xr.ui_clear_color[1], xr.ui_clear_color[2], xr.ui_clear_color[3] );
	xr.ui_active = false;
	if( !GL_OpenXRCheck( xrReleaseSwapchainImage( xr.ui.swapchain, &release_info ), "xrReleaseSwapchainImage(UI)" ))
	{
		GL_OpenXRShutdown();
		xr.failed = true;
		return;
	}
	xr.ui.acquired = false;
	xr.ui.waited = false;
	xr.ui.ready = true;
}

void GL_OpenXRFrameEnd( void )
{
	XrCompositionLayerProjectionView projection_views[REF_VR_MAX_EYES];
	XrCompositionLayerProjection projection = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	XrCompositionLayerQuad ui = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader *layers[2];
	XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
	qboolean submit = xr.frame_begun && xr.frame_state.shouldRender && xr.views_valid;
	uint32_t layer_count = 0;

	if( !xr.frame_begun )
		return;
	if( xr.ui_active )
		GL_OpenXREndUI();
	if( !xr.frame_begun )
		return; /* EndUI may have shut the session down after a release failure. */

	for( int i = 0; i < REF_VR_MAX_EYES; ++i )
	{
		if( xr.eyes[i].acquired && xr.eyes[i].waited )
		{
			XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			GL_OpenXRCheck( xrReleaseSwapchainImage( xr.eyes[i].swapchain, &release_info ),
				"xrReleaseSwapchainImage(interrupted eye)" );
			xr.eyes[i].acquired = false;
			xr.eyes[i].waited = false;
			submit = false;
		}
	}

	for( int i = 0; i < REF_VR_MAX_EYES; ++i )
		submit = submit && xr.eyes[i].ready;

	memset( projection_views, 0, sizeof( projection_views ));
	if( submit )
	{
		projection.space = xr.local_space;
		projection.viewCount = REF_VR_MAX_EYES;
		projection.views = projection_views;
		for( int i = 0; i < REF_VR_MAX_EYES; ++i )
		{
			projection_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projection_views[i].pose = xr.views[i].pose;
			projection_views[i].fov = xr.views[i].fov;
			projection_views[i].subImage.swapchain = xr.eyes[i].swapchain;
			projection_views[i].subImage.imageRect.extent.width = xr.eyes[i].width;
			projection_views[i].subImage.imageRect.extent.height = xr.eyes[i].height;
		}
		layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&projection;
		if( xr.ui.ready )
		{
			float width = bound( 0.25f, vr_ui_width->value, 8.0f );
			ui.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
				XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
			ui.space = xr.head_space;
			ui.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			ui.subImage.swapchain = xr.ui.swapchain;
			ui.subImage.imageRect.extent.width = xr.ui.width;
			ui.subImage.imageRect.extent.height = xr.ui.height;
			ui.pose.orientation.w = 1.0f;
			ui.pose.position.z = -bound( 0.2f, vr_ui_distance->value, 10.0f );
			ui.size.width = width;
			ui.size.height = width * (float)xr.ui.height / (float)xr.ui.width;
			layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&ui;
		}
	}

	end_info.displayTime = xr.frame_state.predictedDisplayTime;
	end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end_info.layerCount = submit ? layer_count : 0;
	end_info.layers = submit ? layers : NULL;
	if( GL_OpenXRCheck( xrEndFrame( xr.session, &end_info ), "xrEndFrame" ) && submit )
	{
		if( !xr.submitted_once )
		{
			xr.submitted_once = true;
			gEngfuncs.Con_Printf( "OpenXR: first stereo frame submitted\n" );
		}
		if( xr.ui.ready && !xr.ui_submitted_once )
		{
			xr.ui_submitted_once = true;
			gEngfuncs.Con_Printf( "OpenXR: first head-relative UI layer submitted\n" );
		}
	}
	xr.frame_begun = false;
	xr.current_eye = -1;
}

qboolean GL_OpenXRHaptic( int hand, float duration, float frequency, float amplitude )
{
	XrHapticActionInfo action_info = { XR_TYPE_HAPTIC_ACTION_INFO };
	XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
	float clamped_duration;

	if( !xr.initialized || !xr.session_running || hand < 0 || hand >= REF_VR_MAX_HANDS ||
		xr.actions.haptic == XR_NULL_HANDLE )
		return false;

	clamped_duration = bound( 0.0f, duration, 10.0f );
	action_info.action = xr.actions.haptic;
	action_info.subactionPath = xr.actions.hand_paths[hand];
	if( clamped_duration <= 0.0f || amplitude <= 0.0f )
		return GL_OpenXRCheck( xrStopHapticFeedback( xr.session, &action_info ), "xrStopHapticFeedback" );

	vibration.duration = (XrDuration)( clamped_duration * 1000000000.0f );
	vibration.frequency = bound( 0.0f, frequency, 320.0f );
	vibration.amplitude = bound( 0.0f, amplitude, 1.0f );
	return GL_OpenXRCheck( xrApplyHapticFeedback( xr.session, &action_info, (const XrHapticBaseHeader *)&vibration ),
		"xrApplyHapticFeedback" );
}

qboolean GL_OpenXRGetEyeFov( float fov[4] )
{
	if( !fov || xr.current_eye < 0 || xr.current_eye >= REF_VR_MAX_EYES || !xr.views_valid )
		return false;

	fov[0] = xr.views[xr.current_eye].fov.angleLeft;
	fov[1] = xr.views[xr.current_eye].fov.angleRight;
	fov[2] = xr.views[xr.current_eye].fov.angleDown;
	fov[3] = xr.views[xr.current_eye].fov.angleUp;
	return true;
}

qboolean GL_OpenXRIsSecondaryEye( void )
{
	return xr.current_eye == 1;
}

qboolean GL_OpenXRIsRenderingEye( void )
{
	return xr.current_eye >= 0 && xr.current_eye < REF_VR_MAX_EYES;
}

void GL_OpenXRApplyEyePose( ref_viewpass_t *rvp )
{
	XrPosef relative;
	ref_vr_pose_t eye;
	vec3_t head_forward, head_right, head_up;
	vec3_t eye_forward, eye_right, eye_up;
	vec3_t composed_forward, composed_right, composed_up;

	if( !rvp || xr.current_eye < 0 || xr.current_eye >= REF_VR_MAX_EYES || !xr.views_valid )
		return;

	relative = GL_OpenXRPoseMultiply( GL_OpenXRPoseInverse( xr.head_pose ), xr.views[xr.current_eye].pose );
	GL_OpenXRFillPose( &relative, &eye );
	AngleVectors( rvp->viewangles, head_forward, head_right, head_up );
	VectorMA( rvp->vieworigin, eye.position[0] * vr_worldscale->value, head_forward, rvp->vieworigin );
	VectorMA( rvp->vieworigin, -eye.position[1] * vr_worldscale->value, head_right, rvp->vieworigin );
	VectorMA( rvp->vieworigin, eye.position[2] * vr_worldscale->value, head_up, rvp->vieworigin );

	VectorCopy( eye.forward, eye_forward );
	VectorCopy( eye.right, eye_right );
	VectorCopy( eye.up, eye_up );
	VectorScale( head_forward, eye_forward[0], composed_forward );
	VectorMA( composed_forward, -eye_forward[1], head_right, composed_forward );
	VectorMA( composed_forward, eye_forward[2], head_up, composed_forward );
	VectorScale( head_forward, eye_right[0], composed_right );
	VectorMA( composed_right, -eye_right[1], head_right, composed_right );
	VectorMA( composed_right, eye_right[2], head_up, composed_right );
	VectorScale( head_forward, eye_up[0], composed_up );
	VectorMA( composed_up, -eye_up[1], head_right, composed_up );
	VectorMA( composed_up, eye_up[2], head_up, composed_up );
	VectorsAngles( composed_forward, composed_right, composed_up, rvp->viewangles );
}

#endif // XASH_OPENXR
