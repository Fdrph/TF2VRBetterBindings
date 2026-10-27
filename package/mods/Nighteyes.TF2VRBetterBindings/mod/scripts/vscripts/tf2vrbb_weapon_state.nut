global function TF2VRBB_WeaponStateInit

void function TF2VRBB_WeaponStateInit()
{
#if TF2VR_COCKPIT
	thread TF2VRBB_WeaponStateThink()
#endif
}

#if TF2VR_COCKPIT
// holster slot of the gun in hand (0 primary, 1 secondary, 3 arc tool), -1 for empty hands
void function TF2VRBB_WeaponStateThink()
{
	while ( true )
	{
		SetConVarInt( "tf2vrbb_active_slot", TF2VRBB_ActiveSlot() )
		WaitFrame()
	}
}

int function TF2VRBB_ActiveSlot()
{
	entity player = GetLocalViewPlayer()
	if ( !IsValid( player ) || !IsAlive( player ) || player.IsTitan() )
		return -1
	entity weapon = player.GetActiveWeapon()
	if ( !IsValid( weapon ) || weapon.IsWeaponOffhand() || TF2VR_EmptyHands( weapon ) )
		return -1
	return TF2VR_GunSlot( weapon )
}
#endif
