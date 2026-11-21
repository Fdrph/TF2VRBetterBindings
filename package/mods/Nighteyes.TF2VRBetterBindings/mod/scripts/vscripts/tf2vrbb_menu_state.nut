global function TF2VRBB_MenuStateInit

void function TF2VRBB_MenuStateInit()
{
#if TF2VR_COCKPIT
	thread TF2VRBB_MenuStateThink()
#endif
}

#if TF2VR_COCKPIT
void function TF2VRBB_MenuStateThink()
{
	while ( true )
	{
		int startup = TF2VR_StartupPhase()
		bool menu = startup == 1 || startup == 2 || ( GetActiveMenu() != null && !uiGlobal.playingVideo )
		SetConVarInt( "tf2vrbb_menu_active", menu ? 1 : 0 )
		WaitFrame()
	}
}
#endif
