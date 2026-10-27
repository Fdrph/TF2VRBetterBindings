global function TF2VRBB_WeaponStateInit

void function TF2VRBB_WeaponStateInit()
{
#if TF2VR_COCKPIT
	thread TF2VRBB_WeaponStateThink()
#endif
}

#if TF2VR_COCKPIT
// trigger kinds, matching the [triggers] keys of the layer
const int TF2VRBB_TRIGGER_NONE = 0
const int TF2VRBB_TRIGGER_PISTOL = 1
const int TF2VRBB_TRIGGER_AUTO = 2
const int TF2VRBB_TRIGGER_MARKSMAN = 3
const int TF2VRBB_TRIGGER_SHOTGUN = 4
const int TF2VRBB_TRIGGER_LAUNCHER = 5
const int TF2VRBB_TRIGGER_ARC_TOOL = 6
const int TF2VRBB_TRIGGER_TITAN = 7

struct {
	table<string, int> triggerKinds
} file

void function TF2VRBB_WeaponStateThink()
{
	foreach ( name in [ "mp_weapon_semipistol", "mp_weapon_wingman", "mp_weapon_wingman_n", "mp_weapon_shotgun_pistol" ] )
		file.triggerKinds[ name ] <- TF2VRBB_TRIGGER_PISTOL
	foreach ( name in [ "mp_weapon_sniper", "mp_weapon_dmr", "mp_weapon_doubletake", "mp_weapon_g2" ] )
		file.triggerKinds[ name ] <- TF2VRBB_TRIGGER_MARKSMAN
	foreach ( name in [ "mp_weapon_shotgun", "mp_weapon_mastiff" ] )
		file.triggerKinds[ name ] <- TF2VRBB_TRIGGER_SHOTGUN
	foreach ( name in [ "mp_weapon_epg", "mp_weapon_softball", "mp_weapon_smr", "mp_weapon_pulse_lmg", "mp_weapon_rocket_launcher", "mp_weapon_defender", "mp_weapon_arc_launcher", "mp_weapon_mgl" ] )
		file.triggerKinds[ name ] <- TF2VRBB_TRIGGER_LAUNCHER
	file.triggerKinds[ CHARGE_TOOL ] <- TF2VRBB_TRIGGER_ARC_TOOL

	while ( true )
	{
		entity player = GetLocalViewPlayer()
		entity weapon = TF2VRBB_HeldWeapon( player )
		SetConVarInt( "tf2vrbb_active_slot", TF2VRBB_ActiveSlot( player, weapon ) )
		SetConVarInt( "tf2vrbb_trigger_kind", TF2VRBB_TriggerKind( player, weapon ) )
		SetConVarInt( "tf2vrbb_clip", TF2VRBB_Clip( weapon ) )
		WaitFrame()
	}
}

// the gun in hand, null for empty hands, offhand weapons and when dead
entity function TF2VRBB_HeldWeapon( entity player )
{
	if ( !IsValid( player ) || !IsAlive( player ) )
		return null
	entity weapon = player.GetActiveWeapon()
	if ( !IsValid( weapon ) || weapon.IsWeaponOffhand() || TF2VR_EmptyHands( weapon ) )
		return null
	return weapon
}

// holster slot of the gun in hand (0 primary, 1 secondary, 3 arc tool), -1 for empty hands
int function TF2VRBB_ActiveSlot( entity player, entity weapon )
{
	if ( weapon == null || player.IsTitan() )
		return -1
	return TF2VR_GunSlot( weapon )
}

int function TF2VRBB_TriggerKind( entity player, entity weapon )
{
	if ( weapon == null )
		return TF2VRBB_TRIGGER_NONE
	if ( player.IsTitan() )
		return TF2VRBB_TRIGGER_TITAN
	string name = weapon.GetWeaponClassName()
	if ( name in file.triggerKinds )
		return file.triggerKinds[ name ]
	return TF2VRBB_TRIGGER_AUTO
}

// rounds in the magazine, -1 for guns without one
int function TF2VRBB_Clip( entity weapon )
{
	if ( weapon == null || weapon.GetWeaponPrimaryClipCountMax() <= 0 )
		return -1
	return weapon.GetWeaponPrimaryClipCount()
}
#endif
