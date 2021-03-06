#ifndef WM_CONTEXTMENU					// Chicago header file?

#define DS_CONTEXTHELP  0x2000L

#define WS_EX_MDICHILD			0x00000040L
#define WS_EX_SMCAPTION 		0x00000080L

#define WS_EX_WINDOWEDGE		0x00000100L
#define WS_EX_CLIENTEDGE		0x00000200L
#define WS_EX_EDGEMASK			(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)
#define WS_EX_CONTEXTHELP		0x00000400L
#if 0
// #define WS_EX_TOOLWINDOW		0x00000800L
#endif

#define WS_EX_RIGHT 			0x00001000L
#define WS_EX_LEFT				0x00000000L
#define WS_EX_RTLREADING		0x00002000L
#define WS_EX_LTRREADING		0x00000000L
#define WS_EX_LEFTSCROLLBAR 	0x00004000L
#define WS_EX_RIGHTSCROLLBAR	0x00000000L

#endif
