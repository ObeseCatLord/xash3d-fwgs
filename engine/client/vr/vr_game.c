/*
vr_game.c - optional VR client-DLL bridge
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "common.h"
#include "client.h"
#include "library.h"
#include "vr_client_api.h"
#include "vr_client.h"
#include "vr_game.h"

static vr_client_api_t vr_game_client;
static qboolean vr_game_initialized;

static void CL_VRGameTransformVector( const float in[3], const ref_vr_pose_t *origin, float out[3] )
{
	out[0] = DotProduct( in, origin->forward );
	out[1] = -DotProduct( in, origin->right );
	out[2] = DotProduct( in, origin->up );
}

static void CL_VRGamePose( vr_client_pose_t *out, const ref_vr_pose_t *pose, const ref_vr_pose_t *center, float world_scale )
{
	vec3_t delta;

	VectorSubtract( pose->position, center->position, delta );
	CL_VRGameTransformVector( delta, center, out->position );
	VectorScale( out->position, world_scale, out->position );
	CL_VRGameTransformVector( pose->forward, center, out->forward );
	CL_VRGameTransformVector( pose->right, center, out->right );
	CL_VRGameTransformVector( pose->up, center, out->up );
}

static int32_t VR_CLIENT_CALL CL_VRGameHaptic( uint32_t hand, float duration, float frequency, float amplitude )
{
	if( hand >= VR_CLIENT_MAX_HANDS )
		return 0;

	return CL_VRHaptic( (int)hand, duration, frequency, amplitude ) ? 1 : 0;
}

void CL_VRGameInit( void )
{
	vr_client_get_api_t GetVRClientAPI;
	vr_client_engine_api_t engine_api =
	{
		.version = VR_CLIENT_API_VERSION,
		.struct_size = sizeof( engine_api ),
		.Haptic = CL_VRGameHaptic,
	};

	CL_VRGameShutdown();
	GetVRClientAPI = (vr_client_get_api_t)COM_GetProcAddress( clgame.hInstance, VR_CLIENT_API_EXPORT );
	if( !GetVRClientAPI )
		return;

	memset( &vr_game_client, 0, sizeof( vr_game_client ));
	/* struct_size is also the writable size supplied to a newer client DLL.
	 * Older v2 DLLs ignore it and report the 16-byte base API on return. */
	vr_game_client.struct_size = sizeof( vr_game_client );
	if( !GetVRClientAPI( &engine_api, &vr_game_client ) || vr_game_client.version != VR_CLIENT_API_VERSION ||
		vr_game_client.struct_size < VR_CLIENT_API_MIN_SIZE || !vr_game_client.Frame || !vr_game_client.Shutdown )
	{
		memset( &vr_game_client, 0, sizeof( vr_game_client ));
		Con_Reportf( S_WARN "VR client API rejected\n" );
		return;
	}

	vr_game_initialized = true;
	Con_Reportf( "VR client API v%u initialized\n", VR_CLIENT_API_VERSION );
}

qboolean CL_VRGameBuildUsercmdSidecar( const usercmd_t *cmd, vr_usercmd_sidecar_t *sample )
{
	vr_client_usercmd_t input;
	vr_usercmd_sidecar_t pose = { 0 };

	if( !vr_game_initialized || !cmd || !sample ||
		vr_game_client.struct_size < VR_CLIENT_API_SIDECAR_SIZE || !vr_game_client.BuildUsercmdSidecar )
		return false;

	input.struct_size = sizeof( input );
	VectorCopy( cmd->viewangles, input.viewangles );
	input.frametime = (float)cmd->msec * ( 1.0f / 1000.0f );
	if( !vr_game_client.BuildUsercmdSidecar( &input, &pose ) ||
		pose.version != VR_USERCMD_SIDECAR_VERSION || !( pose.flags & VR_USERCMD_SIDECAR_POSE_VALID ))
		return false;

	/* Transport owns ladder selection. The game DLL owns only final pose policy. */
	VR_UsercmdSidecarApplyPose( sample, &pose );
	return true;
}

void CL_VRGameFrame( void )
{
	const ref_vr_frame_t *source = CL_VRGetFrame();
	ref_vr_pose_t center;
	vr_client_frame_t frame;
	float world_scale;

	if( !vr_game_initialized )
		return;

	memset( &frame, 0, sizeof( frame ));
	frame.version = VR_CLIENT_FRAME_VERSION;
	frame.struct_size = sizeof( frame );
	if( !CL_VRIsActive() )
	{
		vr_game_client.Frame( &frame );
		return;
	}

	world_scale = CL_VRGetWorldScale();
	frame.frame_id = source->frame_id;
	frame.world_scale = world_scale;
	frame.flags = VR_CLIENT_FRAME_ACTIVE;
	if( FBitSet( source->flags, REF_VR_FRAME_FOCUSED ))
		frame.flags |= VR_CLIENT_FRAME_FOCUSED;
	if( !CL_VRGetRecenter( &center ) )
	{
		vr_game_client.Frame( &frame );
		return;
	}
	if( FBitSet( source->flags, REF_VR_FRAME_HEAD_VALID ))
	{
		frame.flags |= VR_CLIENT_FRAME_HEAD_VALID;
		CL_VRGamePose( &frame.head, &source->head, &center, world_scale );
	}

	for( int hand = 0; hand < VR_CLIENT_MAX_HANDS; ++hand )
	{
		const ref_vr_hand_t *in = &source->hands[hand];
		vr_client_hand_t *out = &frame.hands[hand];
		vec3_t velocity;

		out->buttons = in->buttons;
		out->trigger = in->trigger;
		out->squeeze = in->squeeze;
		out->stick[0] = in->stick[0];
		out->stick[1] = in->stick[1];
		CL_VRGameTransformVector( in->linear_velocity, &center, velocity );
		VectorScale( velocity, world_scale, out->linear_velocity );
		CL_VRGameTransformVector( in->angular_velocity, &center, out->angular_velocity );
		if( FBitSet( in->flags, REF_VR_HAND_GRIP_VALID ))
		{
			out->flags |= VR_CLIENT_HAND_GRIP_VALID;
			CL_VRGamePose( &out->grip, &in->grip, &center, world_scale );
		}
		if( FBitSet( in->flags, REF_VR_HAND_AIM_VALID ))
		{
			out->flags |= VR_CLIENT_HAND_AIM_VALID;
			CL_VRGamePose( &out->aim, &in->aim, &center, world_scale );
		}
	}

	vr_game_client.Frame( &frame );
}

void CL_VRGameShutdown( void )
{
	if( vr_game_initialized )
		vr_game_client.Shutdown();

	vr_game_initialized = false;
	memset( &vr_game_client, 0, sizeof( vr_game_client ));
}
