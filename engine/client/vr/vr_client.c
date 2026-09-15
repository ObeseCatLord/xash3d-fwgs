/*
vr_client.c - renderer-independent client VR frame policy
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "common.h"
#include "client.h"
#include "input.h"
#include "keydefs.h"
#include "vr_client.h"
#include "vr_game.h"

static CVAR_DEFINE_AUTO( vr_enable, "0", FCVAR_ARCHIVE, "enable desktop OpenXR rendering" );
static CVAR_DEFINE_AUTO( vr_worldscale, "40", FCVAR_ARCHIVE, "game units per physical metre" );
static CVAR_DEFINE_AUTO( vr_control_scheme, "0", FCVAR_ARCHIVE, "Lambda1VR control scheme; values >= 10 use the left weapon hand" );
static CVAR_DEFINE_AUTO( vr_comfort_moving, "0", 0, "whether VR locomotion is active for comfort masking" );
static CVAR_DEFINE_AUTO( vr_turn_angle, "45", FCVAR_ARCHIVE, "snap-turn angle, or smooth-turn speed basis" );
static CVAR_DEFINE_AUTO( vr_smoothturn, "0", FCVAR_ARCHIVE, "use continuous turning instead of snap turning" );
static CVAR_DEFINE_AUTO( vr_walkdirection, "1", FCVAR_ARCHIVE, "movement direction: 0 off-hand, 1 head" );
static CVAR_DEFINE_AUTO( vr_reloadtimeoutms, "200", FCVAR_ARCHIVE, "maximum squeeze tap duration used for reload" );
static CVAR_DEFINE_AUTO( vr_enable_crouching, "0.85", FCVAR_ARCHIVE,
	"physical crouch standing-height multiplier; active from 0 to 0.98" );
static CVAR_DEFINE_AUTO( vr_crouch_threshold, "0.32", FCVAR_ARCHIVE,
	"deprecated; physical crouch uses vr_enable_crouching standing-height multiplier" );
static CVAR_DEFINE_AUTO( vr_positional_factor, "1", FCVAR_ARCHIVE, "room-scale movement multiplier" );
static CVAR_DEFINE_AUTO( vr_quick_crouchjump, "1", FCVAR_ARCHIVE, "double-tap jump to perform a crouch jump" );
static CVAR_DEFINE_AUTO( vr_gesture_triggered_use, "1", FCVAR_ARCHIVE, "reach away from the body to use with either hand" );
static CVAR_DEFINE_AUTO( vr_use_gesture_boundary, "0.35", FCVAR_ARCHIVE, "horizontal use-gesture boundary in metres" );
static CVAR_DEFINE_AUTO( vr_controller_tracking_haptic, "1", FCVAR_ARCHIVE, "haptic feedback for tracked interaction boundaries" );
static CVAR_DEFINE_AUTO( vr_backpack_weapon, "weapon_crowbar", FCVAR_ARCHIVE, "weapon selected by the dominant-hand backpack gesture" );
static CVAR_DEFINE_AUTO( vr_lasersight, "0", FCVAR_ARCHIVE, "laser sight mode" );
static CVAR_DEFINE_AUTO( vr_mirror_weapons, "0", FCVAR_ARCHIVE, "mirror weapon viewmodels for left-handed use" );
static CVAR_DEFINE_AUTO( vr_weapon_backface_culling, "0", FCVAR_ARCHIVE, "enable back-face culling on weapon viewmodels" );
static CVAR_DEFINE_AUTO( vr_height_adjust, "0", FCVAR_ARCHIVE, "additional VR eye height in metres" );
static CVAR_DEFINE_AUTO( vr_headtorch, "0", FCVAR_ARCHIVE, "attach the flashlight beam to the HMD" );
static CVAR_DEFINE_AUTO( vr_reversetorch, "0", FCVAR_ARCHIVE, "reverse the tracked flashlight direction" );

static ref_vr_frame_t vr_frame;
static ref_vr_pose_t vr_center;
static qboolean vr_frame_begun;
static qboolean vr_ui_trigger_blocked;
static qboolean vr_center_valid;
static ref_vr_pose_t vr_previous_head;
static qboolean vr_previous_head_valid;
static vec3_t vr_head_delta;
static uint32_t vr_previous_buttons[REF_VR_MAX_HANDS];
static qboolean vr_turn_latched;
static qboolean vr_weapon_latched;
static qboolean vr_reload_pulse;
static double vr_squeeze_time;
static qboolean vr_ui_pressed;
static qboolean vr_ui_cursor_valid;
static float vr_ui_cursor[2];
static qboolean vr_weapon_in_backpack;
static qboolean vr_offhand_in_backpack;
static qboolean vr_backpack_weapon_active;
static qboolean vr_scoreboard_active;
static qboolean vr_offhand_use_active;
static qboolean vr_jump_held;
static qboolean vr_quick_crouch;
static qboolean vr_physical_crouched;
static double vr_last_jump_press;
static qboolean vr_selecting_weapon;
static qboolean vr_confirm_attack_release;
static int vr_weapon_scroll_count;
static qboolean vr_scope_zoom_latched;
static qboolean vr_menu_active;
static qboolean vr_one_controller_shifted;
static qboolean vr_one_controller_squeeze_held;
static qboolean vr_one_controller_jump_mode;
static qboolean vr_one_controller_running;
static qboolean vr_one_controller_duck_toggled;
static double vr_one_controller_jump_time;
static float vr_one_controller_stick[4][2];
static vec3_t vr_body_view_origin;
static float vr_body_view_yaw;
static qboolean vr_body_view_valid;

static void CL_VRTransformVector( const vec3_t in, const ref_vr_pose_t *origin, vec3_t out );

void CL_VRRegisterCvars( void )
{
	Cvar_RegisterVariable( &vr_enable );
	Cvar_RegisterVariable( &vr_worldscale );
	Cvar_RegisterVariable( &vr_control_scheme );
	Cvar_RegisterVariable( &vr_comfort_moving );
	Cvar_RegisterVariable( &vr_turn_angle );
	Cvar_RegisterVariable( &vr_smoothturn );
	Cvar_RegisterVariable( &vr_walkdirection );
	Cvar_RegisterVariable( &vr_reloadtimeoutms );
	Cvar_RegisterVariable( &vr_enable_crouching );
	Cvar_RegisterVariable( &vr_crouch_threshold );
	Cvar_RegisterVariable( &vr_positional_factor );
	Cvar_RegisterVariable( &vr_quick_crouchjump );
	Cvar_RegisterVariable( &vr_gesture_triggered_use );
	Cvar_RegisterVariable( &vr_use_gesture_boundary );
	Cvar_RegisterVariable( &vr_controller_tracking_haptic );
	Cvar_RegisterVariable( &vr_backpack_weapon );
	Cvar_RegisterVariable( &vr_lasersight );
	Cvar_RegisterVariable( &vr_mirror_weapons );
	Cvar_RegisterVariable( &vr_weapon_backface_culling );
	Cvar_RegisterVariable( &vr_height_adjust );
	Cvar_RegisterVariable( &vr_headtorch );
	Cvar_RegisterVariable( &vr_reversetorch );
}

static qboolean CL_VRHandPosition( int hand, vec3_t position )
{
	const ref_vr_hand_t *source = &vr_frame.hands[hand];

	if( FBitSet( source->flags, REF_VR_HAND_GRIP_VALID ))
	{
		VectorCopy( source->grip.position, position );
		return true;
	}
	if( FBitSet( source->flags, REF_VR_HAND_AIM_VALID ))
	{
		VectorCopy( source->aim.position, position );
		return true;
	}
	return false;
}

static qboolean CL_VRIsBackpack( int hand )
{
	vec3_t position, delta, local;
	float horizontal;

	if( !FBitSet( vr_frame.flags, REF_VR_FRAME_HEAD_VALID ) || !CL_VRHandPosition( hand, position ))
		return false;
	VectorSubtract( position, vr_frame.head.position, delta );
	if( VectorLength( delta ) > 0.4f )
		return false;
	CL_VRTransformVector( delta, &vr_frame.head, local );
	horizontal = sqrt( local[0] * local[0] + local[1] * local[1] );
	return horizontal > 0.001f && local[0] < -0.5f * horizontal;
}

static qboolean CL_VRHandSqueezePressed( const ref_vr_hand_t *hand )
{
	return hand && FBitSet( hand->flags, REF_VR_HAND_SQUEEZE_PRESSED );
}

static qboolean CL_VRPhysicalCrouchState( qboolean latched, float multiplier, float standing_height,
	float head_height )
{
	if( multiplier <= 0.0f || multiplier >= 0.98f )
		return false;
	if( !latched )
		return head_height < standing_height * multiplier;
	return head_height <= standing_height * ( multiplier + 0.02f );
}

static void CL_VRResetPhysicalCrouch( void )
{
	vr_physical_crouched = false;
}

static void CL_VRUpdatePhysicalCrouch( void )
{
	if( !vr_center_valid || !FBitSet( vr_frame.flags, REF_VR_FRAME_HEAD_VALID ))
	{
		CL_VRResetPhysicalCrouch();
		return;
	}
	vr_physical_crouched = CL_VRPhysicalCrouchState( vr_physical_crouched, vr_enable_crouching.value,
		vr_center.position[2], vr_frame.head.position[2] );
}

static qboolean CL_VRUseGesture( int hand )
{
	vec3_t position, delta;

	if( !vr_gesture_triggered_use.value || !FBitSet( vr_frame.flags, REF_VR_FRAME_HEAD_VALID ) ||
		!CL_VRHandPosition( hand, position ))
		return false;
	VectorSubtract( position, vr_frame.head.position, delta );
	return sqrt( delta[0] * delta[0] + delta[1] * delta[1] ) > vr_use_gesture_boundary.value;
}

static void CL_VRCommand( const char *command )
{
	char buffer[256];
	Q_snprintf( buffer, sizeof( buffer ), "%s\n", command );
	Cbuf_AddText( buffer );
}

static qboolean CL_VRUIActive( void )
{
	/* GoldSrc VGUI dialogs keep key_game while requesting a visible cursor.
	 * Reuse the same cursor ownership that disables desktop mouse look. */
	return cls.key_dest != key_game || host.mouse_visible;
}

static void CL_VRResetUIPointer( void )
{
	if( vr_ui_pressed && vr_ui_cursor_valid )
		IN_TouchEvent( event_up, 0, vr_ui_cursor[0], vr_ui_cursor[1], 0.0f, 0.0f );
	vr_ui_pressed = false;
	vr_ui_cursor_valid = false;
}

static void CL_VRResetInputState( qboolean release_menu )
{
	CL_VRResetUIPointer();
	vr_ui_trigger_blocked = false;
	Cvar_SetValue( vr_comfort_moving.name, 0.0f );
	if( release_menu )
	{
		if( vr_menu_active )
			Key_Event( K_ESCAPE, false );
		if( vr_scoreboard_active ) Cbuf_AddText( "-showscores\n" );
		if( vr_offhand_use_active ) Cbuf_AddText( "-use2\n" );
	}

	memset( vr_previous_buttons, 0, sizeof( vr_previous_buttons ));
	vr_turn_latched = false;
	vr_weapon_latched = false;
	vr_reload_pulse = false;
	vr_squeeze_time = 0.0;
	vr_weapon_in_backpack = false;
	vr_offhand_in_backpack = false;
	vr_backpack_weapon_active = false;
	vr_scoreboard_active = false;
	vr_offhand_use_active = false;
	vr_jump_held = false;
	vr_quick_crouch = false;
	CL_VRResetPhysicalCrouch();
	vr_last_jump_press = 0.0;
	vr_selecting_weapon = false;
	vr_confirm_attack_release = false;
	vr_weapon_scroll_count = 0;
	vr_scope_zoom_latched = false;
	vr_menu_active = false;
	vr_one_controller_shifted = false;
	vr_one_controller_squeeze_held = false;
	vr_one_controller_jump_mode = false;
	vr_one_controller_running = false;
	vr_one_controller_duck_toggled = false;
	vr_one_controller_jump_time = 0.0;
	memset( vr_one_controller_stick, 0, sizeof( vr_one_controller_stick ));
	vr_body_view_valid = false;
}

static void CL_VRTransformVector( const vec3_t in, const ref_vr_pose_t *origin, vec3_t out )
{
	out[0] = DotProduct( in, origin->forward );
	out[1] = -DotProduct( in, origin->right );
	out[2] = DotProduct( in, origin->up );
}

static void CL_VROneControllerInput( int dominant, int offhand, uint32_t old_dominant_buttons,
	uint32_t old_offhand_buttons )
{
	const ref_vr_hand_t *weapon = &vr_frame.hands[dominant];
	const ref_vr_hand_t *other = &vr_frame.hands[offhand];
	qboolean squeeze = CL_VRHandSqueezePressed( weapon );
	qboolean stick = FBitSet( weapon->buttons, REF_VR_BUTTON_STICK );
	qboolean old_stick = FBitSet( old_dominant_buttons, REF_VR_BUTTON_STICK );

	/* VrInputOne.c uses a short squeeze for reload and its held duration as the
	 * one-controller shift modifier. The shared OpenXR squeeze action supplies
	 * that state identically for either dominant hand. */
	if( squeeze != vr_one_controller_squeeze_held )
	{
		vr_one_controller_squeeze_held = squeeze;
		if( squeeze )
		{
			vr_squeeze_time = 0.0;
			if( !CL_VRIsBackpack( dominant )) vr_squeeze_time = host.realtime;
		}
		else
		{
			if( vr_squeeze_time != 0.0 &&
				( host.realtime - vr_squeeze_time ) * 1000.0 <= vr_reloadtimeoutms.value )
				vr_reload_pulse = true;
			vr_squeeze_time = 0.0;
		}
	}
	vr_one_controller_shifted = squeeze && vr_squeeze_time != 0.0 &&
		( host.realtime - vr_squeeze_time ) * 1000.0 > vr_reloadtimeoutms.value;

	if( !( old_offhand_buttons & REF_VR_BUTTON_STICK ) &&
		FBitSet( other->buttons, REF_VR_BUTTON_STICK ))
		Cvar_SetValue( "vr_lasersight", (int)( vr_lasersight.value + 1.0f ) % 3 );
	if( !( old_offhand_buttons & REF_VR_BUTTON_SECONDARY ) &&
		FBitSet( other->buttons, REF_VR_BUTTON_SECONDARY ))
		Cbuf_AddText( "impulse 100\n" );

	if( vr_one_controller_shifted )
	{
		if( !( old_dominant_buttons & REF_VR_BUTTON_PRIMARY ) &&
			FBitSet( weapon->buttons, REF_VR_BUTTON_PRIMARY ))
		{
			vr_one_controller_jump_mode = !vr_one_controller_jump_mode;
			CL_CenterPrint( vr_one_controller_jump_mode ? "JumpMode:  Duck -> Jump" :
				"JumpMode:  Jump {-> Duck}", -1.0f );
		}
		if( !( old_dominant_buttons & REF_VR_BUTTON_SECONDARY ) &&
			FBitSet( weapon->buttons, REF_VR_BUTTON_SECONDARY ))
		{
			vr_one_controller_running = !vr_one_controller_running;
			CL_CenterPrint( vr_one_controller_running ? "Run Toggle: Enabled" :
				"Run Toggle: Disabled", -1.0f );
		}
	}
	else
	{
		if( stick && !old_stick )
			vr_one_controller_jump_time = host.realtime;
		else if( !stick && old_stick )
			vr_one_controller_jump_time = 0.0;

		/* The Android implementation intentionally toggles duck when the
		 * dominant primary button is released outside the jump macro. */
		if( vr_one_controller_jump_time == 0.0 &&
			( old_dominant_buttons & REF_VR_BUTTON_PRIMARY ) &&
			!FBitSet( weapon->buttons, REF_VR_BUTTON_PRIMARY ))
			vr_one_controller_duck_toggled = !vr_one_controller_duck_toggled;
	}
}

static void CL_VRRelativePose( const ref_vr_pose_t *pose, const ref_vr_pose_t *origin, ref_vr_pose_t *out )
{
	vec3_t delta;

	VectorSubtract( pose->position, origin->position, delta );
	CL_VRTransformVector( delta, origin, out->position );
	CL_VRTransformVector( pose->forward, origin, out->forward );
	CL_VRTransformVector( pose->right, origin, out->right );
	CL_VRTransformVector( pose->up, origin, out->up );
}

void CL_VRFrameBegin( void )
{
	qboolean old_menu, menu;
	uint32_t old_offhand_buttons, old_dominant_buttons;
	int dominant, offhand, variant;

	if( vr_frame_begun )
		CL_VRFrameEnd();

	memset( &vr_frame, 0, sizeof( vr_frame ));
	vr_frame.version = REF_VR_FRAME_VERSION;
	vr_frame.struct_size = sizeof( vr_frame );
	vr_frame_begun = false;

	if( !ref.initialized || !ref.dllFuncs.R_VRFrameBegin )
	{
		CL_VRResetInputState( true );
		return;
	}

	vr_frame_begun = ref.dllFuncs.R_VRFrameBegin( &vr_frame );
	if( !vr_frame_begun || !FBitSet( vr_frame.flags, REF_VR_FRAME_SESSION_ACTIVE ))
	{
		vr_center_valid = false;
		vr_previous_head_valid = false;
		VectorClear( vr_head_delta );
		CL_VRResetInputState( true );
		return;
	}

	if( FBitSet( vr_frame.flags, REF_VR_FRAME_HEAD_VALID ) &&
		( !vr_center_valid || FBitSet( vr_frame.flags, REF_VR_FRAME_REFERENCE_CHANGED )))
	{
		vr_center = vr_frame.head;
		vr_center_valid = true;
		vr_previous_head_valid = false;
		VectorClear( vr_head_delta );
	}

	if( FBitSet( vr_frame.flags, REF_VR_FRAME_HEAD_VALID ))
	{
		if( vr_previous_head_valid )
		{
			VectorSubtract( vr_frame.head.position, vr_previous_head.position, vr_head_delta );
			/* Prediction consumes horizontal room-scale motion this frame. Keep the
			 * rendered offset for vertical crouching, but follow horizontal motion. */
			vr_center.position[0] += vr_head_delta[0];
			vr_center.position[1] += vr_head_delta[1];
		}
		else VectorClear( vr_head_delta );
		vr_previous_head = vr_frame.head;
		vr_previous_head_valid = true;
	}

	dominant = vr_control_scheme.value >= 10.0f ? REF_VR_HAND_LEFT : REF_VR_HAND_RIGHT;
	offhand = dominant == REF_VR_HAND_LEFT ? REF_VR_HAND_RIGHT : REF_VR_HAND_LEFT;
	/* Lambda1VR keeps the legacy viewmodel/game-DLL handedness cvar in lockstep
	 * with its control scheme. Preserve that contract for aware clients. */
	Cvar_SetValue( "hand", dominant == REF_VR_HAND_LEFT ? 1.0f : 0.0f );
	if( !FBitSet( vr_frame.flags, REF_VR_FRAME_FOCUSED ) ||
		!FBitSet( vr_frame.flags, REF_VR_FRAME_ACTIONS_VALID ))
	{
		CL_VRResetInputState( true );
		return;
	}

	if( vr_frame.hands[dominant].trigger < 0.55f )
		vr_ui_trigger_blocked = false;
	if( CL_VRUIActive() )
	{
		ref_vr_hand_t *pointer = &vr_frame.hands[dominant];
		qboolean pressed = pointer->trigger >= 0.55f;
		if( pressed ) vr_ui_trigger_blocked = true;

		if( FBitSet( pointer->flags, REF_VR_HAND_UI_VALID ))
		{
			float dx = vr_ui_cursor_valid ? pointer->ui_cursor[0] - vr_ui_cursor[0] : 0.0f;
			float dy = vr_ui_cursor_valid ? pointer->ui_cursor[1] - vr_ui_cursor[1] : 0.0f;
			vr_ui_cursor[0] = pointer->ui_cursor[0];
			vr_ui_cursor[1] = pointer->ui_cursor[1];
			vr_ui_cursor_valid = true;
			IN_TouchEvent( event_motion, 0, vr_ui_cursor[0], vr_ui_cursor[1], dx, dy );
			if( pressed != vr_ui_pressed )
				IN_TouchEvent( pressed ? event_down : event_up, 0,
					vr_ui_cursor[0], vr_ui_cursor[1], 0.0f, 0.0f );
			vr_ui_pressed = pressed;
		}
		else CL_VRResetUIPointer();
	}
	else CL_VRResetUIPointer();

	old_menu = vr_menu_active;
	menu = (( vr_frame.hands[0].buttons | vr_frame.hands[1].buttons ) & REF_VR_BUTTON_MENU ) != 0 ||
		( FBitSet( vr_frame.hands[0].buttons, REF_VR_BUTTON_STICK ) &&
		FBitSet( vr_frame.hands[1].buttons, REF_VR_BUTTON_STICK ));
	if( menu != old_menu ) Key_Event( K_ESCAPE, menu );
	vr_menu_active = menu;
	if( CL_VRUIActive() )
	{
		for( int i = 0; i < REF_VR_MAX_HANDS; ++i )
			vr_previous_buttons[i] = vr_frame.hands[i].buttons;
		return;
	}

	old_offhand_buttons = vr_previous_buttons[offhand];
	old_dominant_buttons = vr_previous_buttons[dominant];
	variant = (int)vr_control_scheme.value % 10;
	if( variant < 0 || variant > 3 ) variant = 0;
	if( variant == 3 )
	{
		qboolean use;

		CL_VROneControllerInput( dominant, offhand, old_dominant_buttons, old_offhand_buttons );
		use = CL_VRUseGesture( offhand ) && Cvar_VariableValue( "vr_weapon_stabilised" ) == 0.0f &&
			Cmd_Exists( "+use2" );
		if( use != vr_offhand_use_active )
			Cbuf_AddText( use ? "+use2\n" : "-use2\n" );
		vr_offhand_use_active = use;

		for( int i = 0; i < REF_VR_MAX_HANDS; ++i )
			vr_previous_buttons[i] = vr_frame.hands[i].buttons;
		return;
	}
	/* Lambda1VR assigns the off-hand stick click to the laser sight while
	 * unscoped, and to scope smoothing while scoped. Do not also toggle it
	 * when both stick clicks are being used as the desktop menu fallback. */
	if( !( old_offhand_buttons & REF_VR_BUTTON_STICK ) &&
		FBitSet( vr_frame.hands[offhand].buttons, REF_VR_BUTTON_STICK ) &&
		!FBitSet( vr_frame.hands[dominant].buttons, REF_VR_BUTTON_STICK ))
	{
		if( Cvar_VariableValue( "vr_scope_engaged" ) != 0.0f )
			Cvar_SetValue( "vr_scope_stabilise",
				Cvar_VariableValue( "vr_scope_stabilise" ) == 0.0f ? 1.0f : 0.0f );
		else
			Cvar_SetValue( vr_lasersight.name, (int)( vr_lasersight.value + 1.0f ) % 3 );
	}
	if( vr_confirm_attack_release )
	{
		Cbuf_AddText( "-attack\n" );
		vr_confirm_attack_release = false;
	}
	if( variant == 1 &&
		FBitSet( old_offhand_buttons ^ vr_frame.hands[offhand].buttons, REF_VR_BUTTON_SECONDARY ))
	{
		vr_selecting_weapon = FBitSet( vr_frame.hands[offhand].buttons, REF_VR_BUTTON_SECONDARY );
		if( vr_selecting_weapon )
		{
			vr_weapon_scroll_count = 1;
			Cbuf_AddText( "invprev\n" );
		}
		else
		{
			if( vr_weapon_scroll_count == 0 ) Cbuf_AddText( "cancelselect\n" );
			else
			{
				Cbuf_AddText( "+attack\n" );
				vr_confirm_attack_release = true;
			}
			vr_weapon_scroll_count = 0;
		}
	}
	else if( variant != 1 ) vr_selecting_weapon = false;

	{
		qboolean weapon_backpack = CL_VRIsBackpack( dominant );
		qboolean offhand_backpack = CL_VRIsBackpack( offhand );

		if( vr_controller_tracking_haptic.value && weapon_backpack && !vr_weapon_in_backpack && !vr_backpack_weapon_active )
			CL_VRHaptic( dominant, 0.04f, 0.0f, 0.5f );
		if( vr_controller_tracking_haptic.value && offhand_backpack && !vr_offhand_in_backpack )
			CL_VRHaptic( offhand, 0.04f, 0.0f, 0.5f );
		vr_weapon_in_backpack = weapon_backpack;
		vr_offhand_in_backpack = offhand_backpack;
	}

	if( vr_backpack_weapon_active )
	{
		if( !CL_VRHandSqueezePressed( &vr_frame.hands[dominant] ))
		{
			Cbuf_AddText( "lastinv\n" );
			vr_backpack_weapon_active = false;
		}
	}
	else if( vr_weapon_in_backpack && CL_VRHandSqueezePressed( &vr_frame.hands[dominant] ))
	{
		CL_VRCommand( vr_backpack_weapon.string );
		CL_VRHaptic( dominant, 0.08f, 0.0f, 0.8f );
		vr_backpack_weapon_active = true;
		vr_squeeze_time = 0.0;
	}
	else if( !vr_weapon_in_backpack )
	{
		if( CL_VRHandSqueezePressed( &vr_frame.hands[dominant] ) && vr_squeeze_time == 0.0 )
			vr_squeeze_time = host.realtime;
		else if( !CL_VRHandSqueezePressed( &vr_frame.hands[dominant] ) && vr_squeeze_time != 0.0 )
		{
			if(( host.realtime - vr_squeeze_time ) * 1000.0 <= vr_reloadtimeoutms.value )
			{
				vr_reload_pulse = true;
				CL_VRHaptic( dominant, 0.04f, 0.0f, 0.5f );
			}
			vr_squeeze_time = 0.0;
		}
	}

	if( vr_offhand_in_backpack && cl.maxclients <= 1 )
	{
		if( !( old_offhand_buttons & REF_VR_BUTTON_PRIMARY ) &&
			( vr_frame.hands[offhand].buttons & REF_VR_BUTTON_PRIMARY ))
			Cbuf_AddText( "savequick\n" );
		if( !( old_offhand_buttons & REF_VR_BUTTON_SECONDARY ) &&
			( vr_frame.hands[offhand].buttons & REF_VR_BUTTON_SECONDARY ))
			Cbuf_AddText( "loadquick\n" );
	}
	else
	{
		int flashlight_hand = variant == 2 ? dominant : offhand;
		uint32_t old_flashlight = flashlight_hand == dominant ? old_dominant_buttons : old_offhand_buttons;
		if(( old_flashlight & REF_VR_BUTTON_PRIMARY ) &&
			!( vr_frame.hands[flashlight_hand].buttons & REF_VR_BUTTON_PRIMARY ))
			Cbuf_AddText( "impulse 100\n" );

		if( variant == 0 || variant == 2 )
		{
			int screen_hand = variant == 2 ? dominant : offhand;
			uint32_t old_screen = screen_hand == dominant ? old_dominant_buttons : old_offhand_buttons;
			if( !( old_screen & REF_VR_BUTTON_SECONDARY ) &&
				( vr_frame.hands[screen_hand].buttons & REF_VR_BUTTON_SECONDARY ))
			{
				vr_scoreboard_active = !vr_scoreboard_active;
				if( cl.maxclients > 1 )
					Cbuf_AddText( vr_scoreboard_active ? "+showscores\n" : "-showscores\n" );
			}
		}
	}

	{
		int jump_hand = variant == 2 ? offhand : dominant;
		uint32_t old_jump = jump_hand == dominant ? old_dominant_buttons : old_offhand_buttons;
		qboolean jump = FBitSet( vr_frame.hands[jump_hand].buttons, REF_VR_BUTTON_SECONDARY );
		qboolean old_jump_pressed = FBitSet( old_jump, REF_VR_BUTTON_SECONDARY );

		if( jump && !old_jump_pressed )
		{
			vr_quick_crouch = vr_quick_crouchjump.value && vr_last_jump_press > 0.0 &&
				( host.realtime - vr_last_jump_press ) <= 0.3;
			vr_last_jump_press = host.realtime;
			vr_jump_held = true;
		}
		else if( !jump && old_jump_pressed )
		{
			vr_jump_held = false;
			vr_quick_crouch = false;
		}
	}

	{
		qboolean use = CL_VRUseGesture( offhand ) &&
			Cvar_VariableValue( "vr_weapon_stabilised" ) == 0.0f && Cmd_Exists( "+use2" );
		if( use != vr_offhand_use_active )
			Cbuf_AddText( use ? "+use2\n" : "-use2\n" );
		vr_offhand_use_active = use;
	}

	if( variant == 1 )
	{
		float axis = vr_frame.hands[dominant].stick[0];
		if( vr_selecting_weapon && fabs( axis ) > 0.4f )
		{
			if( !vr_weapon_latched )
			{
				Cbuf_AddText( axis > 0.0f ? "invprev\n" : "invnext\n" );
				vr_weapon_scroll_count += axis > 0.0f ? 1 : -1;
				vr_weapon_latched = true;
			}
		}
		else vr_weapon_latched = false;
	}
	else if( Cvar_VariableValue( "vr_scope_engaged" ) != 0.0f )
	{
		float axis = vr_frame.hands[offhand].stick[1];
		if( fabs( axis ) > 0.6f )
		{
			if( !vr_scope_zoom_latched )
			{
				Cbuf_AddText( axis > 0.0f ? "impulse 104\n" : "impulse 105\n" );
				vr_scope_zoom_latched = true;
			}
		}
		else vr_scope_zoom_latched = false;
	}
	else
	{
		int selector = variant == 2 ? offhand : dominant;
		float threshold = variant == 2 ? 0.75f : 0.65f;
		float xaxis = vr_frame.hands[selector].stick[0];
		float yaxis = vr_frame.hands[selector].stick[1];
		qboolean choose = fabs( yaxis ) > threshold ||
			( vr_turn_angle.value == 0.0f && fabs( xaxis ) > threshold );

		if( choose )
		{
			if( !vr_weapon_latched )
			{
				if( fabs( yaxis ) > threshold )
					Cbuf_AddText( yaxis > 0.0f ? "invprev\n" : "invnext\n" );
				else Cbuf_AddText( xaxis > 0.0f ? "invprevslot\n" : "invnextslot\n" );
				vr_weapon_latched = true;
			}
		}
		else vr_weapon_latched = false;
	}

	for( int i = 0; i < REF_VR_MAX_HANDS; ++i )
		vr_previous_buttons[i] = vr_frame.hands[i].buttons;
}

void CL_VRFrameEnd( void )
{
	if( vr_frame_begun && ref.initialized && ref.dllFuncs.R_VRFrameEnd )
		ref.dllFuncs.R_VRFrameEnd();

	vr_frame_begun = false;
}

qboolean CL_VRShouldRender( void )
{
	return vr_frame_begun && vr_center_valid && vr_frame.eye_count == REF_VR_MAX_EYES &&
		FBitSet( vr_frame.flags, REF_VR_FRAME_SHOULD_RENDER );
}

qboolean CL_VRBeginEye( int eye )
{
	return CL_VRShouldRender() && ref.dllFuncs.R_VRBeginEye && ref.dllFuncs.R_VRBeginEye( eye );
}

qboolean CL_VREndEye( int eye )
{
	return ref.dllFuncs.R_VREndEye && ref.dllFuncs.R_VREndEye( eye, refState.width, refState.height );
}

static void CL_VRClampHeadOffset( const vec3_t start, const vec3_t desired, vec3_t clamped )
{
	pmtrace_t trace;
	vec3_t trace_start, trace_end;

	VectorCopy( desired, clamped );
	if( cls.spectator || !clgame.pmove )
		return;

	VectorCopy( start, trace_start );
	VectorCopy( desired, trace_end );
	trace = CL_TraceLine( trace_start, trace_end, PM_STUDIO_IGNORE );
	if( trace.fraction < 1.0f )
	{
		/* Match Lambda1VR's wall pushback: clip room-scale XY at the first
		 * obstruction while preserving the physically requested head Z. */
		clamped[0] = trace.endpos[0];
		clamped[1] = trace.endpos[1];
	}
}

void CL_VRApplyHeadPose( ref_viewpass_t *rvp )
{
	ref_vr_pose_t relative;
	vec3_t body_forward, body_right, body_up;
	vec3_t composed_forward, composed_right, composed_up;
	vec3_t head_start, desired_origin;

	if( !rvp || !vr_center_valid || !FBitSet( vr_frame.flags, REF_VR_FRAME_HEAD_VALID ))
	{
		vr_body_view_valid = false;
		return;
	}

	CL_VRRelativePose( &vr_frame.head, &vr_center, &relative );
	VectorCopy( rvp->vieworigin, vr_body_view_origin );
	vr_body_view_yaw = rvp->viewangles[YAW];
	vr_body_view_valid = true;
	AngleVectors( rvp->viewangles, body_forward, body_right, body_up );
	VectorMA( rvp->vieworigin, vr_height_adjust.value * vr_worldscale.value, body_up, rvp->vieworigin );
	VectorCopy( rvp->vieworigin, head_start );
	VectorMA( head_start, relative.position[0] * vr_worldscale.value, body_forward, desired_origin );
	VectorMA( desired_origin, -relative.position[1] * vr_worldscale.value, body_right, desired_origin );
	VectorMA( desired_origin, relative.position[2] * vr_worldscale.value, body_up, desired_origin );
	CL_VRClampHeadOffset( head_start, desired_origin, rvp->vieworigin );

	VectorScale( body_forward, relative.forward[0], composed_forward );
	VectorMA( composed_forward, -relative.forward[1], body_right, composed_forward );
	VectorMA( composed_forward, relative.forward[2], body_up, composed_forward );
	VectorScale( body_forward, relative.right[0], composed_right );
	VectorMA( composed_right, -relative.right[1], body_right, composed_right );
	VectorMA( composed_right, relative.right[2], body_up, composed_right );
	VectorScale( body_forward, relative.up[0], composed_up );
	VectorMA( composed_up, -relative.up[1], body_right, composed_up );
	VectorMA( composed_up, relative.up[2], body_up, composed_up );
	VectorsAngles( composed_forward, composed_right, composed_up, rvp->viewangles );
}

uint64_t CL_VRFrameId( void )
{
	return vr_frame.frame_id;
}

static float CL_VRFilterAxis( float value )
{
	const float deadzone = 0.2f;
	float magnitude = fabs( value );

	if( magnitude <= deadzone ) return 0.0f;
	magnitude = ( magnitude - deadzone ) / ( 1.0f - deadzone );
	return copysign( magnitude * magnitude, value );
}

static void CL_VROneControllerMoveAxis( const ref_vr_hand_t *weapon, float *x, float *y )
{
	float average_x = 0.0f, average_y = 0.0f;
	float distance, filtered;

	for( int i = ARRAYSIZE( vr_one_controller_stick ) - 1; i > 0; --i )
	{
		vr_one_controller_stick[i][0] = vr_one_controller_stick[i - 1][0];
		vr_one_controller_stick[i][1] = vr_one_controller_stick[i - 1][1];
	}
	vr_one_controller_stick[0][0] = weapon->stick[0];
	vr_one_controller_stick[0][1] = weapon->stick[1];
	for( int i = 0; i < ARRAYSIZE( vr_one_controller_stick ); ++i )
	{
		average_x += vr_one_controller_stick[i][0];
		average_y += vr_one_controller_stick[i][1];
	}
	average_x /= ARRAYSIZE( vr_one_controller_stick );
	average_y /= ARRAYSIZE( vr_one_controller_stick );
	distance = sqrt( average_x * average_x + average_y * average_y );
	if( distance <= 0.2f ) filtered = 0.0f;
	else filtered = pow( ( distance - 0.2f ) / 0.8f, 2.2f );
	*x = filtered * average_x;
	*y = filtered * average_y;
}

void CL_VRAppendMove( float frametime, usercmd_t *cmd, qboolean active )
{
	int dominant, offhand, variant;
	ref_vr_hand_t *weapon, *move, *turn;
	float x, y, heading, sine, cosine;
	float forward_speed, side_speed;
	vec3_t head_delta;
	ref_vr_pose_t relative_aim, relative_head;
	vec3_t aim_angles, head_angles;

	if( !cmd || !active || CL_VRUIActive() || !CL_VRIsActive() ||
		!FBitSet( vr_frame.flags, REF_VR_FRAME_FOCUSED ) ||
		!FBitSet( vr_frame.flags, REF_VR_FRAME_ACTIONS_VALID ))
	{
		Cvar_SetValue( vr_comfort_moving.name, 0.0f );
		return;
	}

	dominant = vr_control_scheme.value >= 10.0f ? REF_VR_HAND_LEFT : REF_VR_HAND_RIGHT;
	offhand = dominant == REF_VR_HAND_LEFT ? REF_VR_HAND_RIGHT : REF_VR_HAND_LEFT;
	variant = (int)vr_control_scheme.value % 10;
	if( variant < 0 || variant > 3 ) variant = 0;
	weapon = &vr_frame.hands[dominant];
	move = &vr_frame.hands[variant == 2 || variant == 3 ? dominant : offhand];
	turn = &vr_frame.hands[variant == 2 ? offhand : dominant];

	if( variant == 3 )
	{
		if( vr_one_controller_shifted )
			x = y = 0.0f;
		else CL_VROneControllerMoveAxis( weapon, &x, &y );
	}
	else
	{
		x = CL_VRFilterAxis( move->stick[0] );
		y = CL_VRFilterAxis( move->stick[1] );
	}
	if( Cvar_VariableValue( "vr_scope_engaged" ) != 0.0f )
		x = y = 0.0f;
	Cvar_SetValue( vr_comfort_moving.name, fabs( x ) + fabs( y ) > 0.01f ||
		( variant != 3 && vr_smoothturn.value && fabs( turn->stick[0] ) > 0.6f ) ? 1.0f : 0.0f );
	heading = 0.0f;
	if( variant != 3 && vr_walkdirection.value < 0.5f && FBitSet( move->flags, REF_VR_HAND_AIM_VALID ) &&
		FBitSet( vr_frame.flags, REF_VR_FRAME_HEAD_VALID ))
	{
		CL_VRRelativePose( &move->aim, &vr_center, &relative_aim );
		CL_VRRelativePose( &vr_frame.head, &vr_center, &relative_head );
		VectorsAngles( relative_aim.forward, relative_aim.right, relative_aim.up, aim_angles );
		VectorsAngles( relative_head.forward, relative_head.right, relative_head.up, head_angles );
		heading = DEG2RAD( aim_angles[YAW] - head_angles[YAW] );
	}
	SinCos( heading, &sine, &cosine );
	forward_speed = Cvar_VariableValue( "cl_forwardspeed" );
	side_speed = Cvar_VariableValue( "cl_sidespeed" );
	if( forward_speed <= 0.0f ) forward_speed = 400.0f;
	if( side_speed <= 0.0f ) side_speed = 400.0f;
	cmd->sidemove += ( x * cosine - y * sine ) * side_speed;
	cmd->forwardmove += ( y * cosine + x * sine ) * forward_speed;

	if( frametime > 0.0f )
	{
		CL_VRTransformVector( vr_head_delta, &vr_center, head_delta );
		cmd->forwardmove += bound( -forward_speed, head_delta[0] * vr_worldscale.value * vr_positional_factor.value / frametime, forward_speed );
		cmd->sidemove -= bound( -side_speed, head_delta[1] * vr_worldscale.value * vr_positional_factor.value / frametime, side_speed );
	}

	/* The client DLL has already normalized keyboard input. VR is appended at
	 * the engine boundary, so complete the command contract here as well. */
	if( cl.local.maxspeed > 0.0f )
	{
		float speed = sqrt( cmd->forwardmove * cmd->forwardmove +
			cmd->sidemove * cmd->sidemove + cmd->upmove * cmd->upmove );
		if( speed > cl.local.maxspeed )
		{
			float scale = cl.local.maxspeed / speed;
			cmd->forwardmove *= scale;
			cmd->sidemove *= scale;
			cmd->upmove *= scale;
		}
	}
	ClearBits( cmd->buttons, IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT );
	if( cmd->forwardmove > 1.0f ) SetBits( cmd->buttons, IN_FORWARD );
	else if( cmd->forwardmove < -1.0f ) SetBits( cmd->buttons, IN_BACK );
	if( cmd->sidemove > 1.0f ) SetBits( cmd->buttons, IN_MOVERIGHT );
	else if( cmd->sidemove < -1.0f ) SetBits( cmd->buttons, IN_MOVELEFT );

	if( variant == 3 )
	{
		if( !vr_ui_trigger_blocked && weapon->trigger >= 0.55f ) SetBits( cmd->buttons, IN_ATTACK );
		if( !vr_one_controller_shifted && FBitSet( weapon->buttons, REF_VR_BUTTON_SECONDARY ))
			SetBits( cmd->buttons, IN_ATTACK2 );
		if( vr_one_controller_running ) SetBits( cmd->buttons, IN_RUN );
		if( vr_one_controller_shifted && FBitSet( weapon->buttons, REF_VR_BUTTON_STICK ))
			SetBits( cmd->buttons, IN_USE );
	}
	else
	{
		if( !vr_ui_trigger_blocked && weapon->trigger >= 0.55f )
			SetBits( cmd->buttons, CL_VRHandSqueezePressed( weapon ) ? IN_ATTACK2 : IN_ATTACK );
		if( move->trigger >= 0.55f ) SetBits( cmd->buttons, IN_RUN );
		if( FBitSet( weapon->buttons, REF_VR_BUTTON_STICK )) SetBits( cmd->buttons, IN_USE );
	}
	if( CL_VRUseGesture( dominant ) && Cvar_VariableValue( "vr_weapon_stabilised" ) == 0.0f )
		SetBits( cmd->buttons, IN_USE );
	if(( variant == 3 ? vr_one_controller_duck_toggled : variant == 2 ? FBitSet( vr_frame.hands[offhand].buttons, REF_VR_BUTTON_PRIMARY ) :
		FBitSet( weapon->buttons, REF_VR_BUTTON_PRIMARY )) || vr_quick_crouch )
		SetBits( cmd->buttons, IN_DUCK );
	if( variant == 3 && vr_one_controller_jump_time != 0.0 )
	{
		if( vr_one_controller_jump_mode )
		{
			SetBits( cmd->buttons, IN_DUCK );
			if( host.realtime - vr_one_controller_jump_time > 0.25 ) SetBits( cmd->buttons, IN_JUMP );
		}
		else
		{
			SetBits( cmd->buttons, IN_JUMP );
			if( host.realtime - vr_one_controller_jump_time > 0.25 ) SetBits( cmd->buttons, IN_DUCK );
		}
	}
	else if( vr_jump_held ) SetBits( cmd->buttons, IN_JUMP );
	CL_VRUpdatePhysicalCrouch();
	if( vr_physical_crouched )
		SetBits( cmd->buttons, IN_DUCK );
	if( vr_reload_pulse )
	{
		SetBits( cmd->buttons, IN_RELOAD );
		vr_reload_pulse = false;
	}

	if( variant == 3 )
	{
		float xaxis = weapon->stick[0];
		float yaxis = weapon->stick[1];
		qboolean choose = fabs( yaxis ) > 0.75f || fabs( xaxis ) > 0.75f;

		if( vr_one_controller_shifted && !FBitSet( weapon->buttons, REF_VR_BUTTON_STICK ) && choose )
		{
			if( !vr_weapon_latched )
			{
				if( yaxis > 0.75f ) Cbuf_AddText( "invnext\n" );
				else if( yaxis < -0.75f ) Cbuf_AddText( "invprev\n" );
				else if( xaxis > 0.75f ) Cbuf_AddText( "invprevslot\n" );
				else Cbuf_AddText( "invnextslot\n" );
				vr_weapon_latched = true;
			}
		}
		else vr_weapon_latched = false;
	}
	else if( vr_smoothturn.value )
	{
		x = turn->stick[0];
		if( vr_selecting_weapon ) x = 0.0f;
		if( fabs( x ) > 0.2f )
			cmd->viewangles[YAW] -= CL_VRFilterAxis( x ) * vr_turn_angle.value * 8.0f * frametime;
	}
	else
	{
		x = turn->stick[0];
		if( vr_selecting_weapon ) x = 0.0f;
		if( fabs( x ) > 0.7f )
		{
			if( !vr_turn_latched )
			{
				cmd->viewangles[YAW] -= copysign( vr_turn_angle.value, x );
				vr_turn_latched = true;
			}
		}
		else if( fabs( x ) < 0.2f ) vr_turn_latched = false;
	}

	COM_NormalizeAngles( cmd->viewangles );
	VectorCopy( cmd->viewangles, cl.viewangles );
}

void CL_VRBuildUsercmdSidecar( const usercmd_t *cmd, vr_usercmd_sidecar_t *sample )
{
	const ref_vr_hand_t *offhand;
	ref_vr_pose_t relativeOffhand;
	vec3_t offhandAngles;
	int dominant, support;

	if( !sample )
		return;
	memset( sample, 0, sizeof( *sample ));
	if( !cmd || !CL_VRIsActive() || !vr_center_valid ||
		!FBitSet( vr_frame.flags, REF_VR_FRAME_ACTIONS_VALID ))
		return;

	dominant = vr_control_scheme.value >= 10.0f ? REF_VR_HAND_LEFT : REF_VR_HAND_RIGHT;
	support = dominant == REF_VR_HAND_LEFT ? REF_VR_HAND_RIGHT : REF_VR_HAND_LEFT;
	offhand = &vr_frame.hands[support];
	if( Cvar_VariableValue( "vr_controller_ladders" ) != 0.0f &&
		FBitSet( offhand->flags, REF_VR_HAND_AIM_VALID ))
	{
		CL_VRRelativePose( &offhand->aim, &vr_center, &relativeOffhand );
		VectorsAngles( relativeOffhand.forward, relativeOffhand.right, relativeOffhand.up, offhandAngles );
		offhandAngles[YAW] += cmd->viewangles[YAW];
		COM_NormalizeAngles( offhandAngles );
		if( offhandAngles[PITCH] == offhandAngles[PITCH] && offhandAngles[YAW] == offhandAngles[YAW] )
		{
			sample->version = VR_USERCMD_SIDECAR_VERSION;
			sample->flags = VR_USERCMD_SIDECAR_LADDER_VALID;
			sample->ladder_angles[0] = VR_UsercmdSidecarQuantize( offhandAngles[PITCH], 128.0f );
			sample->ladder_angles[1] = VR_UsercmdSidecarQuantize( offhandAngles[YAW], 128.0f );
		}
	}

	CL_VRGameBuildUsercmdSidecar( cmd, sample );
}

qboolean CL_VRHaptic( int hand, float duration, float frequency, float amplitude )
{
	return ref.initialized && ref.dllFuncs.R_VRHaptic &&
		ref.dllFuncs.R_VRHaptic( hand, duration, frequency, amplitude );
}

const ref_vr_frame_t *CL_VRGetFrame( void )
{
	return &vr_frame;
}

qboolean CL_VRIsActive( void )
{
	return vr_frame_begun && FBitSet( vr_frame.flags, REF_VR_FRAME_SESSION_ACTIVE );
}

qboolean CL_VRGetRecenter( ref_vr_pose_t *center )
{
	if( !center || !vr_center_valid )
		return false;

	*center = vr_center;
	return true;
}

float CL_VRGetWorldScale( void )
{
	return vr_worldscale.value;
}

qboolean CL_VRGetFlashlightPose( vec3_t origin, vec3_t forward )
{
	ref_vr_pose_t relative;
	const ref_vr_pose_t *pose;
	float sine, cosine;
	vec3_t local_origin;

	if( !CL_VRIsActive() || !vr_center_valid || !vr_body_view_valid )
		return false;

	if( vr_headtorch.value != 0.0f )
	{
		if( !FBitSet( vr_frame.flags, REF_VR_FRAME_HEAD_VALID ))
			return false;
		pose = &vr_frame.head;
	}
	else
	{
		int dominant = vr_control_scheme.value >= 10.0f ? REF_VR_HAND_LEFT : REF_VR_HAND_RIGHT;
		int offhand = dominant == REF_VR_HAND_LEFT ? REF_VR_HAND_RIGHT : REF_VR_HAND_LEFT;
		if( !FBitSet( vr_frame.hands[offhand].flags, REF_VR_HAND_AIM_VALID ))
			return false;
		pose = &vr_frame.hands[offhand].aim;
	}

	CL_VRRelativePose( pose, &vr_center, &relative );
	SinCos( DEG2RAD( vr_body_view_yaw ), &sine, &cosine );
	local_origin[0] = relative.position[0] * cosine - relative.position[1] * sine;
	local_origin[1] = relative.position[0] * sine + relative.position[1] * cosine;
	local_origin[2] = relative.position[2];
	VectorMA( vr_body_view_origin, vr_worldscale.value, local_origin, origin );
	forward[0] = relative.forward[0] * cosine - relative.forward[1] * sine;
	forward[1] = relative.forward[0] * sine + relative.forward[1] * cosine;
	forward[2] = relative.forward[2];
	if( vr_reversetorch.value != 0.0f )
		VectorNegate( forward, forward );
	VectorNormalize( forward );
	return true;
}

#if XASH_ENGINE_TESTS
#include "tests.h"

void Test_RunVRInputPolicy( void )
{
	int saved_dest = cls.key_dest;
	qboolean saved_cursor = host.mouse_visible;
	cls.key_dest = key_game;
	host.mouse_visible = false;
	TASSERT( !CL_VRUIActive() );
	host.mouse_visible = true;
	TASSERT( CL_VRUIActive() ); /* Sven MOTD/menu, without changing key_dest. */
	host.mouse_visible = false;
	cls.key_dest = key_menu;
	TASSERT( CL_VRUIActive() );
	cls.key_dest = saved_dest;
	host.mouse_visible = saved_cursor;
	vr_ui_trigger_blocked = true;
	TASSERT( !REF_VR_SQUEEZE_PRESSED( 0.5f ));
	TASSERT( REF_VR_SQUEEZE_PRESSED( 0.5001f ));
	TASSERT( CL_VRPhysicalCrouchState( false, 0.85f, 1.0f, 0.84f ));
	TASSERT( !CL_VRPhysicalCrouchState( false, 0.85f, 1.0f, 0.86f ));
	TASSERT( CL_VRPhysicalCrouchState( true, 0.85f, 1.0f, 0.86f ));
	TASSERT( !CL_VRPhysicalCrouchState( true, 0.85f, 1.0f, 0.88f ));
	TASSERT( !CL_VRPhysicalCrouchState( true, 0.0f, 1.80f, 1.00f ));
	vr_physical_crouched = true;
	/* Focus and session loss both take this reset path. */
	CL_VRResetInputState( false );
	TASSERT( !vr_ui_trigger_blocked );
	TASSERT( !vr_physical_crouched );
	vr_physical_crouched = true;
	vr_center_valid = false;
	CL_VRUpdatePhysicalCrouch();
	TASSERT( !vr_physical_crouched );
}
#endif
