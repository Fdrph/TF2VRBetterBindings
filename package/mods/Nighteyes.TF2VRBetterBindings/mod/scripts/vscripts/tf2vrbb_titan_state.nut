global function TF2VRBB_TitanStateInit

void function TF2VRBB_TitanStateInit()
{
	thread TF2VRBB_TitanStateThink()
}

void function TF2VRBB_TitanStateThink()
{
	while ( true )
	{
		entity player = GetLocalViewPlayer()
		SetConVarInt( "tf2vrbb_titan", IsValid( player ) && player.IsTitan() ? 1 : 0 )
		WaitFrame()
	}
}
