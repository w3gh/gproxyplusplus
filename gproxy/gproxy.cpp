/*

   Copyright 2010 Trevor Hogan

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

// todotodo: GHost++ may drop the player even after they reconnect if they run out of time and haven't caught up yet

#include "gproxy.h"
#include "util.h"
#include "config.h"
#include "socket.h"
#include "commandpacket.h"
#include "bnetprotocol.h"
#include "bnet.h"
#include "gameprotocol.h"
#include "gpsprotocol.h"

#include <signal.h>
#include <stdlib.h>

#ifdef WIN32
 #include <windows.h>
 #include <winsock.h>
#endif

#include <time.h>

#ifndef WIN32
 #include <sys/time.h>
#endif

#ifdef __APPLE__
 #include <mach/mach_time.h>
#endif

#ifdef WIN32
 #include "curses.h"
#else
 #include <curses.h>
#endif

bool gCurses = false;
vector<string> gMainBuffer;
string gInputBuffer;
string gChannelName;
vector<string> gChannelUsers;
WINDOW *gMainWindow = NULL;
WINDOW *gBottomBorder = NULL;
WINDOW *gRightBorder = NULL;
WINDOW *gInputWindow = NULL;
WINDOW *gChannelWindow = NULL;
bool gMainWindowChanged = false;
bool gInputWindowChanged = false;
bool gChannelWindowChanged = false;

string gLogFile;
CGProxy *gGProxy = NULL;

uint32_t GetTime( )
{
	return GetTicks( ) / 1000;
}

uint32_t GetTicks( )
{
#ifdef WIN32
	return timeGetTime( );
#elif __APPLE__
	uint64_t current = mach_absolute_time( );
	static mach_timebase_info_data_t info = { 0, 0 };
	// get timebase info
	if( info.denom == 0 )
		mach_timebase_info( &info );
	uint64_t elapsednano = current * ( info.numer / info.denom );
	// convert ns to ms
	return elapsednano / 1e6;
#else
	uint32_t ticks;
	struct timespec t;
	clock_gettime( CLOCK_MONOTONIC, &t );
	ticks = t.tv_sec * 1000;
	ticks += t.tv_nsec / 1000000;
	return ticks;
#endif
}

void LOG_Print( string message )
{
	if( !gLogFile.empty( ) )
	{
		ofstream Log;
		Log.open( gLogFile.c_str( ), ios :: app );

		if( !Log.fail( ) )
		{
			time_t Now = time( NULL );
			string Time = asctime( localtime( &Now ) );

			// erase the newline

			Time.erase( Time.size( ) - 1 );
			Log << "[" << Time << "] " << message << endl;
			Log.close( );
		}
	}
}

void CONSOLE_Print( string message, bool log )
{
	CONSOLE_PrintNoCRLF( message, log );

	if( !gCurses )
		cout << endl;
}

void CONSOLE_PrintNoCRLF( string message, bool log )
{
	gMainBuffer.push_back( message );

	if( gMainBuffer.size( ) > 100 )
		gMainBuffer.erase( gMainBuffer.begin( ) );

	gMainWindowChanged = true;
	CONSOLE_Draw( );

	if( log )
		LOG_Print( message );

	if( !gCurses )
		cout << message;
}

void CONSOLE_ChangeChannel( string channel )
{
	gChannelName = channel;
	gChannelWindowChanged = true;
}

void CONSOLE_AddChannelUser( string name )
{
	for( vector<string> :: iterator i = gChannelUsers.begin( ); i != gChannelUsers.end( ); i++ )
	{
		if( *i == name )
			return;
	}

	gChannelUsers.push_back( name );
	gChannelWindowChanged = true;
}

void CONSOLE_RemoveChannelUser( string name )
{
	for( vector<string> :: iterator i = gChannelUsers.begin( ); i != gChannelUsers.end( ); )
	{
		if( *i == name )
			i = gChannelUsers.erase( i );
		else
			i++;
	}

	gChannelWindowChanged = true;
}

void CONSOLE_RemoveChannelUsers( )
{
	gChannelUsers.clear( );
	gChannelWindowChanged = true;
}

void CONSOLE_Draw( )
{
	if( !gCurses )
		return;

	// draw main window

	if( gMainWindowChanged )
	{
		wclear( gMainWindow );
		wmove( gMainWindow, 0, 0 );

		for( vector<string> :: iterator i = gMainBuffer.begin( ); i != gMainBuffer.end( ); i++ )
		{
			for( string :: iterator j = (*i).begin( ); j != (*i).end( ); j++ )
				waddch( gMainWindow, *j );

			if( i != gMainBuffer.end( ) - 1 )
				waddch( gMainWindow, '\n' );
		}

		wrefresh( gMainWindow );
		gMainWindowChanged = false;
	}

	// draw input window

	if( gInputWindowChanged )
	{
		wclear( gInputWindow );
		wmove( gInputWindow, 0, 0 );

		for( string :: iterator i = gInputBuffer.begin( ); i != gInputBuffer.end( ); i++ )
			waddch( gInputWindow, *i );

		wrefresh( gInputWindow );
		gInputWindowChanged = false;
	}

	// draw channel window

	if( gChannelWindowChanged )
	{
		wclear( gChannelWindow );
		mvwaddnstr( gChannelWindow, 0, gChannelName.size( ) < 16 ? ( 16 - gChannelName.size( ) ) / 2 : 0, gChannelName.c_str( ), 16 );
		mvwhline( gChannelWindow, 1, 0, 0, 16 );
		int y = 2;

		for( vector<string> :: iterator i = gChannelUsers.begin( ); i != gChannelUsers.end( ); i++ )
		{
			mvwaddnstr( gChannelWindow, y, 0, (*i).c_str( ), 16 );
			y++;

			if( y >= LINES - 3 )
				break;
		}

		wrefresh( gChannelWindow );
		gChannelWindowChanged = false;
	}
}

void CONSOLE_Resize( )
{
	if( !gCurses )
		return;

	wresize( gMainWindow, LINES - 3, COLS - 17 );
	wresize( gBottomBorder, 1, COLS );
	wresize( gRightBorder, LINES - 3, 1 );
	wresize( gInputWindow, 2, COLS );
	wresize( gChannelWindow, LINES - 3, 16 );
	// mvwin( gMainWindow, 0, 0 );
	mvwin( gBottomBorder, LINES - 3, 0 );
	mvwin( gRightBorder, 0, COLS - 17 );
	mvwin( gInputWindow, LINES - 2, 0 );
	mvwin( gChannelWindow, 0, COLS - 16 );
	mvwhline( gBottomBorder, 0, 0, 0, COLS );
	mvwvline( gRightBorder, 0, 0, 0, LINES );
	wrefresh( gBottomBorder );
	wrefresh( gRightBorder );
	gMainWindowChanged = true;
	gInputWindowChanged = true;
	gChannelWindowChanged = true;
	CONSOLE_Draw( );
}

//
// main
//

int main( int argc, char **argv )
{
	string CFGFile = "gproxy.cfg";

	if( argc > 1 && argv[1] )
		CFGFile = argv[1];

	// read config file

	CConfig CFG;
	CFG.Read( CFGFile );
	gLogFile = CFG.GetString( "log", string( ) );

	CONSOLE_Print( "[GPROXY] starting up" );

#ifndef WIN32
	// disable SIGPIPE since some systems like OS X don't define MSG_NOSIGNAL

	signal( SIGPIPE, SIG_IGN );
#endif

#ifdef WIN32
	// initialize winsock

	CONSOLE_Print( "[GPROXY] starting winsock" );
	WSADATA wsadata;

	if( WSAStartup( MAKEWORD( 2, 2 ), &wsadata ) != 0 )
	{
		CONSOLE_Print( "[GPROXY] error starting winsock" );
		return 1;
	}

	// increase process priority

	CONSOLE_Print( "[GPROXY] setting process priority to \"above normal\"" );
	SetPriorityClass( GetCurrentProcess( ), ABOVE_NORMAL_PRIORITY_CLASS );
#endif

	string War3Path;
	string CDKeyROC;
	string CDKeyTFT;
	string Server;
	string Username;
	string Password;
	string Channel;
	uint32_t War3Version = 24;
	uint16_t Port = 6125;
	BYTEARRAY EXEVersion;
	BYTEARRAY EXEVersionHash;
	string PasswordHashType;

	if( !CFG.Exists( "war3path" ) || !CFG.Exists( "cdkeyroc" ) || !CFG.Exists( "server" ) || !CFG.Exists( "username" ) || !CFG.Exists( "password" ) || !CFG.Exists( "channel" ) )
	{
		CONSOLE_Print( "", false );
		CONSOLE_Print( "  It looks like this is your first time running GProxy++.", false );
		CONSOLE_Print( "  GProxy++ needs some information about your computer and about yourself.", false );
		CONSOLE_Print( "", false );
		CONSOLE_Print( "  This information will be saved to the file \"" + CFGFile + "\" in plain text.", false );
		CONSOLE_Print( "  You can update this information at any time by editing the above file.", false );
		CONSOLE_Print( "", false );

#ifdef WIN32
		string RegistryWar3Path;
		HKEY hkey;
		LSTATUS s = RegOpenKeyExA( HKEY_CURRENT_USER, "Software\\Blizzard Entertainment\\Warcraft III", 0, KEY_QUERY_VALUE, &hkey );

		if( s == ERROR_SUCCESS )
		{
			char InstallPath[256];
			DWORD InstallPathSize = 256;
			RegQueryValueExA( hkey, "InstallPath", NULL, NULL, (LPBYTE)InstallPath, &InstallPathSize );
			RegistryWar3Path = InstallPath;
			RegCloseKey( hkey );
		}

		CONSOLE_Print( "  Enter the path to your Warcraft III install folder.", false );
		CONSOLE_Print( "  If you want to use the detected path just leave it blank (press enter).", false );
		CONSOLE_Print( "", false );
		CONSOLE_PrintNoCRLF( "  Install Path [" + RegistryWar3Path + "]: ", false );
		getline( cin, War3Path );

		if( War3Path.empty( ) )
			War3Path = RegistryWar3Path;
#else
		CONSOLE_Print( "  GProxy++ needs some files from a Windows installation of Warcraft III.", false );
		CONSOLE_Print( "  These files are:", false );
		CONSOLE_Print( "    game.dll", false );
		CONSOLE_Print( "    storm.dll", false );
		CONSOLE_Print( "    war3.exe", false );
		CONSOLE_Print( "  Enter the path to the directory containing these files.", false );
		CONSOLE_Print( "", false );
		CONSOLE_PrintNoCRLF( "  Install Path: ", false );
		getline( cin, War3Path );
#endif
		War3Path = UTIL_AddPathSeperator( War3Path );

		CONSOLE_Print( "", false );
		CONSOLE_Print( "  Enter your CD key(s) with or without dashes, capital letters or lowercase.", false );
		CONSOLE_Print( "", false );

		do
		{
			CONSOLE_PrintNoCRLF( "  Reign of Chaos CD key: ", false );
			getline( cin, CDKeyROC );
			CDKeyROC.erase( remove( CDKeyROC.begin( ), CDKeyROC.end( ), '-' ), CDKeyROC.end( ) );
			transform( CDKeyROC.begin( ), CDKeyROC.end( ), CDKeyROC.begin( ), (int(*)(int))toupper );
		} while( CDKeyROC.size( ) != 26 );

		CONSOLE_Print( "", false );

		do
		{
			CONSOLE_PrintNoCRLF( "  Frozen Throne CD key: ", false );
			getline( cin, CDKeyTFT );
			CDKeyTFT.erase( remove( CDKeyTFT.begin( ), CDKeyTFT.end( ), '-' ), CDKeyTFT.end( ) );
			transform( CDKeyTFT.begin( ), CDKeyTFT.end( ), CDKeyTFT.begin( ), (int(*)(int))toupper );
		} while( !CDKeyTFT.empty( ) && CDKeyTFT.size( ) != 26 );

		CONSOLE_Print( "", false );
		CONSOLE_Print( "  Select a battle.net server to connect to.", false );
		CONSOLE_Print( "  Enter one of the following numbers (1-4) or enter a custom address.", false );
		CONSOLE_Print( "  1. US West (Lordaeron)", false );
		CONSOLE_Print( "  2. US East (Azeroth)", false );
		CONSOLE_Print( "  3. Asia (Kalimdor)", false );
		CONSOLE_Print( "  4. Europe (Northrend)", false );
		CONSOLE_Print( "", false );

		do
		{
			CONSOLE_PrintNoCRLF( "  Server: ", false );
			getline( cin, Server );
			transform( Server.begin( ), Server.end( ), Server.begin( ), (int(*)(int))tolower );

			if( Server == "1" || Server == "1." || Server == "us west" || Server == "uswest" || Server == "west" || Server == "lordaeron" )
				Server = "uswest.battle.net";
			else if( Server == "2" || Server == "2." || Server == "us east" || Server == "useast" || Server == "east" || Server == "azeroth" )
				Server = "useast.battle.net";
			else if( Server == "3" || Server == "3." || Server == "asia" || Server == "kalimdor" )
				Server = "asia.battle.net";
			else if( Server == "4" || Server == "4." || Server == "europe" || Server == "northrend" )
				Server = "europe.battle.net";
		} while( Server.empty( ) );

		CONSOLE_Print( "", false );
		CONSOLE_Print( "  What is your battle.net username?", false );
		CONSOLE_Print( "", false );

		do
		{
			CONSOLE_PrintNoCRLF( "  Username: ", false );
			getline( cin, Username );
		} while( Username.empty( ) );

		CONSOLE_Print( "", false );
		CONSOLE_Print( "  What is your battle.net password?", false );
		CONSOLE_Print( "  Note that your password will be visible as you type it.", false );
		CONSOLE_Print( "", false );

		do
		{
			CONSOLE_PrintNoCRLF( "  Password: ", false );
			getline( cin, Password );
		} while( Password.empty( ) );

		CONSOLE_Print( "", false );
		CONSOLE_Print( "  What battle.net channel do you want to start in?", false );
		CONSOLE_Print( "", false );

		do
		{
			CONSOLE_PrintNoCRLF( "  Channel: ", false );
			getline( cin, Channel );
		} while( Channel.empty( ) );

		CONSOLE_Print( "", false );
		CONSOLE_Print( "  Done. Saving configuration to \"" + CFGFile + "\".", false );

		ofstream out;
		out.open( CFGFile.c_str( ) );

		if( out.fail( ) )
			CONSOLE_Print( "  Error saving configuration file.", false );
		else
		{
			out << "### required config values" << endl;
			out << endl;
			out << "war3path = " << War3Path << endl;
			out << "cdkeyroc = " << CDKeyROC << endl;
			out << "cdkeytft = " << CDKeyTFT << endl;
			out << "server = " << Server << endl;
			out << "username = " << Username << endl;
			out << "password = " << Password << endl;
			out << "channel = " << Channel << endl;
			out << endl;
			out << "### optional config values" << endl;
			out << endl;
			out << "war3version = " << War3Version << endl;
			out << "port = " << Port << endl;
			out << "exeversion =" << endl;
			out << "exeversionhash =" << endl;
			out << "passwordhashtype =" << endl;
			out.close( );
		}
	}
	else
	{
		War3Path = CFG.GetString( "war3path", string( ) );
		CDKeyROC = CFG.GetString( "cdkeyroc", string( ) );
		CDKeyTFT = CFG.GetString( "cdkeytft", string( ) );
		Server = CFG.GetString( "server", string( ) );
		Username = CFG.GetString( "username", string( ) );
		Password = CFG.GetString( "password", string( ) );
		Channel = CFG.GetString( "channel", string( ) );
		War3Version = CFG.GetInt( "war3version", War3Version );
		Port = CFG.GetInt( "port", Port );
		EXEVersion = UTIL_ExtractNumbers( CFG.GetString( "exeversion", string( ) ), 4 );
		EXEVersionHash = UTIL_ExtractNumbers( CFG.GetString( "exeversionhash", string( ) ), 4 );
		PasswordHashType = CFG.GetString( "passwordhashtype", string( ) );
	}

	CONSOLE_Print( "", false );
	CONSOLE_Print( "  Welcome to GProxy++.", false );
	CONSOLE_Print( "  Server: " + Server, false );
	CONSOLE_Print( "  Username: " + Username, false );
	CONSOLE_Print( "  Channel: " + Channel, false );
	CONSOLE_Print( "", false );

	// initialize curses

	gCurses = true;
	initscr( );
#ifdef WIN32
	resize_term( 28, 97 );
#endif
	clear( );
	noecho( );
	cbreak( );
	gMainWindow = newwin( LINES - 3, COLS - 17, 0, 0 );
	gBottomBorder = newwin( 1, COLS, LINES - 3, 0 );
	gRightBorder = newwin( LINES - 3, 1, 0, COLS - 17 );
	gInputWindow = newwin( 2, COLS, LINES - 2, 0 );
	gChannelWindow = newwin( LINES - 3, 16, 0, COLS - 16 );
	mvwhline( gBottomBorder, 0, 0, 0, COLS );
	mvwvline( gRightBorder, 0, 0, 0, LINES );
	wrefresh( gBottomBorder );
	wrefresh( gRightBorder );
	scrollok( gMainWindow, TRUE );
	keypad( gInputWindow, TRUE );
	scrollok( gInputWindow, TRUE );
	CONSOLE_Print( "  Type /help at any time for help.", false );
	CONSOLE_Print( "  Press any key to continue.", false );
	CONSOLE_Print( "", false );
	CONSOLE_Draw( );
	wgetch( gInputWindow );
	nodelay( gInputWindow, TRUE );

	// initialize gproxy

	gGProxy = new CGProxy( !CDKeyTFT.empty( ), War3Path, CDKeyROC, CDKeyTFT, Server, Username, Password, Channel, War3Version, Port, EXEVersion, EXEVersionHash, PasswordHashType );

	while( 1 )
	{
		if( gGProxy->Update( 40000 ) )
			break;

		bool Quit = false;
		int c = wgetch( gInputWindow );

		while( c != ERR )
		{
			if( c == 8 || c == 127 || c == KEY_BACKSPACE || c == KEY_DC )
			{
				// backspace, delete

				if( !gInputBuffer.empty( ) )
					gInputBuffer.erase( gInputBuffer.size( ) - 1, 1 );
			}
			else if( c == 9 )
			{
				// tab
			}
#ifdef WIN32
			else if( c == 10 || c == 13 || c == PADENTER )
#else
			else if( c == 10 || c == 13 )
#endif
			{
				// cr, lf
				// process input buffer now

				string Command = gInputBuffer;
				transform( Command.begin( ), Command.end( ), Command.begin( ), (int(*)(int))tolower );

				if( Command == "/commands" )
				{
					CONSOLE_Print( ">>> /commands" );
					CONSOLE_Print( "", false );
					CONSOLE_Print( "  In the GProxy++ console:", false );
					CONSOLE_Print( "   /commands           : show command list", false );
					CONSOLE_Print( "   /exit or /quit      : close GProxy++", false );
					CONSOLE_Print( "   /filter <f>         : start filtering public game names for <f>", false );
					CONSOLE_Print( "   /filteroff          : stop filtering public game names", false );
					CONSOLE_Print( "   /game <gamename>    : look for a specific game named <gamename>", false );
					CONSOLE_Print( "   /help               : show help text", false );
					CONSOLE_Print( "   /public             : enable listing of public games", false );
					CONSOLE_Print( "   /publicoff          : disable listing of public games", false );
					CONSOLE_Print( "   /r <message>        : reply to the last received whisper", false );
#ifdef WIN32
					CONSOLE_Print( "   /start              : start warcraft 3", false );
#endif
					CONSOLE_Print( "   /version            : show version text", false );
					CONSOLE_Print( "", false );
					CONSOLE_Print( "  In game:", false );
					CONSOLE_Print( "   /re <message>       : reply to the last received whisper", false );
					CONSOLE_Print( "   /sc                 : whispers \"spoofcheck\" to the game host", false );
					CONSOLE_Print( "   /status             : show status information", false );
					CONSOLE_Print( "   /w <user> <message> : whispers <message> to <user>", false );
					CONSOLE_Print( "", false );
				}
				else if( Command == "/exit" || Command == "/quit" )
				{
					Quit = true;
					break;
				}
				else if( Command.size( ) >= 9 && Command.substr( 0, 8 ) == "/filter " )
				{
					string Filter = gInputBuffer.substr( 8 );

					if( !Filter.empty( ) && Filter.size( ) <= 31 )
					{
						gGProxy->m_BNET->SetPublicGameFilter( Filter );
						CONSOLE_Print( "[BNET] started filtering public game names for \"" + Filter + "\"" );
					}
				}
				else if( Command == "/filteroff" )
				{
					gGProxy->m_BNET->SetPublicGameFilter( string( ) );
					CONSOLE_Print( "[BNET] stopped filtering public game names" );
				}
				else if( Command.size( ) >= 7 && Command.substr( 0, 6 ) == "/game " )
				{
					string GameName = gInputBuffer.substr( 6 );

					if( !GameName.empty( ) && GameName.size( ) <= 31 )
					{
						gGProxy->m_BNET->SetSearchGameName( GameName );
						CONSOLE_Print( "[BNET] looking for a game named \"" + GameName + "\" for up to two minutes" );
					}
				}
				else if( Command == "/help" )
				{
					CONSOLE_Print( ">>> /help" );
					CONSOLE_Print( "", false );
					CONSOLE_Print( "  GProxy++ connects to battle.net and looks for games for you to join.", false );
					CONSOLE_Print( "  If GProxy++ finds any they will be listed on the Warcraft III LAN screen.", false );
					CONSOLE_Print( "  To join a game, simply open Warcraft III and wait at the LAN screen.", false );
					CONSOLE_Print( "  Standard games will be white and GProxy++ enabled games will be blue.", false );
					CONSOLE_Print( "  Note: You must type \"/public\" to enable listing of public games.", false );
					CONSOLE_Print( "", false );
					CONSOLE_Print( "  If you want to join a specific game, type \"/game <gamename>\".", false );
					CONSOLE_Print( "  GProxy++ will look for that game for up to two minutes before giving up.", false );
					CONSOLE_Print( "  This is useful for private games.", false );
					CONSOLE_Print( "", false );
					CONSOLE_Print( "  Please note:", false );
					CONSOLE_Print( "  GProxy++ will join the game using your battle.net name, not your LAN name.", false );
					CONSOLE_Print( "  Other players will see your battle.net name even if you choose another name.", false );
					CONSOLE_Print( "  This is done so that you can be automatically spoof checked.", false );
					CONSOLE_Print( "", false );
					CONSOLE_Print( "  Type \"/commands\" for a full command list.", false );
					CONSOLE_Print( "", false );
				}
				else if( Command == "/public" || Command == "/publicon" || Command == "/public on" || Command == "/list" || Command == "/liston" || Command == "/list on" )
				{
					gGProxy->m_BNET->SetListPublicGames( true );
					CONSOLE_Print( "[BNET] listing of public games enabled" );
				}
				else if( Command == "/publicoff" || Command == "/public off" || Command == "/listoff" || Command == "/list off" )
				{
					gGProxy->m_BNET->SetListPublicGames( false );
					CONSOLE_Print( "[BNET] listing of public games disabled" );
				}
				else if( Command.size( ) >= 4 && Command.substr( 0, 3 ) == "/r " )
				{
					if( !gGProxy->m_BNET->GetReplyTarget( ).empty( ) )
						gGProxy->m_BNET->QueueChatCommand( gInputBuffer.substr( 3 ), gGProxy->m_BNET->GetReplyTarget( ), true );
					else
						CONSOLE_Print( "[BNET] nobody has whispered you yet" );
				}
#ifdef WIN32
				else if( Command == "/start" )
				{
					STARTUPINFO si;
					PROCESS_INFORMATION pi;
					ZeroMemory( &si, sizeof( si ) );
					si.cb = sizeof( si );
					ZeroMemory( &pi, sizeof( pi ) );
					string War3EXE;

					if( !CDKeyTFT.empty( ) )
						War3EXE = War3Path + "Frozen Throne.exe";
					else
						War3EXE = War3Path + "Warcraft III.exe";

					BOOL hProcess = CreateProcessA( War3EXE.c_str( ), NULL, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, War3Path.c_str( ), LPSTARTUPINFOA( &si ), &pi );

					if( !hProcess )
						CONSOLE_Print( "[GPROXY] failed to start warcraft 3" );
					else
					{
						CONSOLE_Print( "[GPROXY] started warcraft 3" );
						CloseHandle( pi.hProcess );
						CloseHandle( pi.hThread );
					}
				}
#endif
				else if( Command == "/version" )
					CONSOLE_Print( "[GPROXY] GProxy++ Version " + gGProxy->m_Version );
				else
					gGProxy->m_BNET->QueueChatCommand( gInputBuffer );

				gInputBuffer.clear( );
			}
#ifdef WIN32
			else if( c == 22 )
			{
				// paste

				char *clipboard = NULL;
				long length = 0;

				if( PDC_getclipboard( &clipboard, &length ) == PDC_CLIP_SUCCESS )
				{
					gInputBuffer += string( clipboard, length );
					PDC_freeclipboard( clipboard );
				}
			}
#endif
			else if( c == 27 )
			{
				// esc

				gInputBuffer.clear( );
			}
			else if( c >= 32 && c <= 255 )
			{
				// printable characters

				gInputBuffer.push_back( c );
			}
#ifdef WIN32
			else if( c == PADSLASH )
				gInputBuffer.push_back( '/' );
			else if( c == PADSTAR )
				gInputBuffer.push_back( '*' );
			else if( c == PADMINUS )
				gInputBuffer.push_back( '-' );
			else if( c == PADPLUS )
				gInputBuffer.push_back( '+' );
#endif
			else if( c == KEY_RESIZE )
				CONSOLE_Resize( );

			// clamp input buffer size

			if( gInputBuffer.size( ) > 200 )
				gInputBuffer.erase( 200 );

			c = wgetch( gInputWindow );
			gInputWindowChanged = true;
		}

		CONSOLE_Draw( );

		if( Quit )
			break;
	}

	// shutdown gproxy

	CONSOLE_Print( "[GPROXY] shutting down" );
	delete gGProxy;
	gGProxy = NULL;

#ifdef WIN32
	// shutdown winsock

	CONSOLE_Print( "[GPROXY] shutting down winsock" );
	WSACleanup( );
#endif

	// shutdown curses

	endwin( );
	return 0;
}

//
// CGProxy
//

CGProxy :: CGProxy( bool nTFT, string nWar3Path, string nCDKeyROC, string nCDKeyTFT, string nServer, string nUsername, string nPassword, string nChannel, uint32_t nWar3Version, uint16_t nPort, BYTEARRAY nEXEVersion, BYTEARRAY nEXEVersionHash, string nPasswordHashType )
{
	m_Version = "Public Test Release 1.0 (March 11, 2010)";
	m_LocalServer = new CTCPServer( );
	m_LocalSocket = NULL;
	m_RemoteSocket = new CTCPClient( );
	m_RemoteSocket->SetNoDelay( true );
	m_UDPSocket = new CUDPSocket( );
	m_UDPSocket->SetBroadcastTarget( "127.0.0.1" );
	m_GameProtocol = new CGameProtocol( this );
	m_GPSProtocol = new CGPSProtocol( );
	m_TotalPacketsReceivedFromLocal = 0;
	m_TotalPacketsReceivedFromRemote = 0;
	m_Exiting = false;
	m_TFT = nTFT;
	m_War3Path = nWar3Path;
	m_CDKeyROC = nCDKeyROC;
	m_CDKeyTFT = nCDKeyTFT;
	m_Server = nServer;
	m_Username = nUsername;
	m_Password = nPassword;
	m_Channel = nChannel;
	m_War3Version = nWar3Version;
	m_Port = nPort;
	m_LastConnectionAttemptTime = 0;
	m_LastRefreshTime = 0;
	m_RemoteServerPort = 0;
	m_GameIsReliable = false;
	m_GameStarted = false;
	m_LeaveGameSent = false;
	m_ActionReceived = false;
	m_Synchronized = true;
	m_ReconnectPort = 0;
	m_PID = 255;
	m_ChatPID = 255;
	m_ReconnectKey = 0;
	m_NumEmptyActions = 0;
	m_NumEmptyActionsUsed = 0;
	m_LastAckTime = 0;
	m_LastActionTime = 0;
	m_BNET = new CBNET( this, m_Server, string( ), 0, 0, m_CDKeyROC, m_CDKeyTFT, "USA", "United States", m_Username, m_Password, m_Channel, m_War3Version, nEXEVersion, nEXEVersionHash, nPasswordHashType, 200 );
	m_LocalServer->Listen( string( ), m_Port );
	CONSOLE_Print( "[GPROXY] GProxy++ Version " + m_Version );
}

CGProxy :: ~CGProxy( )
{
	for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); i++ )
		m_UDPSocket->Broadcast( 6112, m_GameProtocol->SEND_W3GS_DECREATEGAME( (*i)->GetUniqueGameID( ) ) );

	delete m_LocalServer;
	delete m_LocalSocket;
	delete m_RemoteSocket;
	delete m_UDPSocket;
	delete m_BNET;

	for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); i++ )
		delete *i;

	delete m_GameProtocol;
	delete m_GPSProtocol;

	while( !m_LocalPackets.empty( ) )
	{
		delete m_LocalPackets.front( );
		m_LocalPackets.pop( );
	}

	while( !m_RemotePackets.empty( ) )
	{
		delete m_RemotePackets.front( );
		m_RemotePackets.pop( );
	}

	while( !m_PacketBuffer.empty( ) )
	{
		delete m_PacketBuffer.front( );
		m_PacketBuffer.pop( );
	}
}

bool CGProxy :: Update( long usecBlock )
{
	unsigned int NumFDs = 0;

	// take every socket we own and throw it in one giant select statement so we can block on all sockets

	int nfds = 0;
	fd_set fd;
	fd_set send_fd;
	FD_ZERO( &fd );
	FD_ZERO( &send_fd );

	// 1. the battle.net socket

	NumFDs += m_BNET->SetFD( &fd, &send_fd, &nfds );

	// 2. the local server

	m_LocalServer->SetFD( &fd, &send_fd, &nfds );
	NumFDs++;

	// 3. the local socket

	if( m_LocalSocket )
	{
		m_LocalSocket->SetFD( &fd, &send_fd, &nfds );
		NumFDs++;
	}

	// 4. the remote socket

	if( !m_RemoteSocket->HasError( ) && m_RemoteSocket->GetConnected( ) )
	{
		m_RemoteSocket->SetFD( &fd, &send_fd, &nfds );
		NumFDs++;
	}

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = usecBlock;

	struct timeval send_tv;
	send_tv.tv_sec = 0;
	send_tv.tv_usec = 0;

#ifdef WIN32
	select( 1, &fd, NULL, NULL, &tv );
	select( 1, NULL, &send_fd, NULL, &send_tv );
#else
	select( nfds + 1, &fd, NULL, NULL, &tv );
	select( nfds + 1, NULL, &send_fd, NULL, &send_tv );
#endif

	if( NumFDs == 0 )
		MILLISLEEP( 50 );

	if( m_BNET->Update( &fd, &send_fd ) )
		return true;

	//
	// accept new connections
	//

	CTCPSocket *NewSocket = m_LocalServer->Accept( &fd );

	if( NewSocket )
	{
		if( m_LocalSocket )
		{
			// someone's already connected, reject the new connection
			// we only allow one person to use the proxy at a time

			delete NewSocket;
		}
		else
		{
			CONSOLE_Print( "[GPROXY] local player connected" );
			m_LocalSocket = NewSocket;
			m_LocalSocket->SetNoDelay( true );
			m_TotalPacketsReceivedFromLocal = 0;
			m_TotalPacketsReceivedFromRemote = 0;
			m_GameIsReliable = false;
			m_GameStarted = false;
			m_LeaveGameSent = false;
			m_ActionReceived = false;
			m_Synchronized = true;
			m_ReconnectPort = 0;
			m_PID = 255;
			m_ChatPID = 255;
			m_ReconnectKey = 0;
			m_NumEmptyActions = 0;
			m_NumEmptyActionsUsed = 0;
			m_LastAckTime = 0;
			m_LastActionTime = 0;
			m_JoinedName.clear( );
			m_HostName.clear( );

			while( !m_PacketBuffer.empty( ) )
			{
				delete m_PacketBuffer.front( );
				m_PacketBuffer.pop( );
			}
		}
	}

	if( m_LocalSocket )
	{
		//
		// handle proxying (reconnecting, etc...)
		//

		if( m_LocalSocket->HasError( ) || !m_LocalSocket->GetConnected( ) )
		{
			CONSOLE_Print( "[GPROXY] local player disconnected" );

			if( m_BNET->GetInGame( ) )
				m_BNET->QueueEnterChat( );

			delete m_LocalSocket;
			m_LocalSocket = NULL;

			// ensure a leavegame message was sent, otherwise the server may wait for our reconnection which will never happen
			// if one hasn't been sent it's because Warcraft III exited abnormally

			if( m_GameIsReliable && !m_LeaveGameSent )
			{
				// note: we're not actually 100% ensuring the leavegame message is sent, we'd need to check that DoSend worked, etc...

				BYTEARRAY LeaveGame;
				LeaveGame.push_back( 0xF7 );
				LeaveGame.push_back( 0x21 );
				LeaveGame.push_back( 0x08 );
				LeaveGame.push_back( 0x00 );
				UTIL_AppendByteArray( LeaveGame, (uint32_t)PLAYERLEAVE_GPROXY, false );
				m_RemoteSocket->PutBytes( LeaveGame );
				m_RemoteSocket->DoSend( &send_fd );
			}

			m_RemoteSocket->Reset( );
			m_RemoteSocket->SetNoDelay( true );
			m_RemoteServerIP.clear( );
			m_RemoteServerPort = 0;
		}
		else
		{
			m_LocalSocket->DoRecv( &fd );
			ExtractLocalPackets( );
			ProcessLocalPackets( );

			if( !m_RemoteServerIP.empty( ) )
			{
				if( m_GameIsReliable && m_ActionReceived && GetTime( ) - m_LastActionTime >= 60 )
				{
					if( m_NumEmptyActionsUsed < m_NumEmptyActions )
					{
						SendEmptyAction( );
						m_NumEmptyActionsUsed++;
					}
					else
					{
						SendLocalChat( "GProxy++ ran out of time to reconnect, Warcraft III will disconnect soon." );
						CONSOLE_Print( "[GPROXY] ran out of time to reconnect" );
					}

					m_LastActionTime = GetTime( );
				}

				if( m_RemoteSocket->HasError( ) )
				{
					CONSOLE_Print( "[GPROXY] disconnected from remote server due to socket error" );

					if( m_GameIsReliable && m_ActionReceived && m_ReconnectPort > 0 )
					{
						SendLocalChat( "You have been disconnected from the server due to a socket error." );
						uint32_t TimeRemaining = ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 - ( GetTime( ) - m_LastActionTime );

						if( GetTime( ) - m_LastActionTime > ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 )
							TimeRemaining = 0;

						SendLocalChat( "GProxy++ is attempting to reconnect... (" + UTIL_ToString( TimeRemaining ) + " seconds remain)" );
						CONSOLE_Print( "[GPROXY] attempting to reconnect" );
						m_RemoteSocket->Reset( );
						m_RemoteSocket->SetNoDelay( true );
						m_RemoteSocket->Connect( string( ), m_RemoteServerIP, m_ReconnectPort );
						m_LastConnectionAttemptTime = GetTime( );
					}
					else
					{
						if( m_BNET->GetInGame( ) )
							m_BNET->QueueEnterChat( );

						m_LocalSocket->Disconnect( );
						delete m_LocalSocket;
						m_LocalSocket = NULL;
						m_RemoteSocket->Reset( );
						m_RemoteSocket->SetNoDelay( true );
						m_RemoteServerIP.clear( );
						m_RemoteServerPort = 0;
						return false;
					}
				}

				if( !m_RemoteSocket->GetConnecting( ) && !m_RemoteSocket->GetConnected( ) )
				{
					CONSOLE_Print( "[GPROXY] disconnected from remote server" );

					if( m_GameIsReliable && m_ActionReceived && m_ReconnectPort > 0 )
					{
						SendLocalChat( "You have been disconnected from the server." );
						uint32_t TimeRemaining = ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 - ( GetTime( ) - m_LastActionTime );

						if( GetTime( ) - m_LastActionTime > ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 )
							TimeRemaining = 0;

						SendLocalChat( "GProxy++ is attempting to reconnect... (" + UTIL_ToString( TimeRemaining ) + " seconds remain)" );
						CONSOLE_Print( "[GPROXY] attempting to reconnect" );
						m_RemoteSocket->Reset( );
						m_RemoteSocket->SetNoDelay( true );
						m_RemoteSocket->Connect( string( ), m_RemoteServerIP, m_ReconnectPort );
						m_LastConnectionAttemptTime = GetTime( );
					}
					else
					{
						if( m_BNET->GetInGame( ) )
							m_BNET->QueueEnterChat( );

						m_LocalSocket->Disconnect( );
						delete m_LocalSocket;
						m_LocalSocket = NULL;
						m_RemoteSocket->Reset( );
						m_RemoteSocket->SetNoDelay( true );
						m_RemoteServerIP.clear( );
						m_RemoteServerPort = 0;
						return false;
					}
				}

				if( m_RemoteSocket->GetConnected( ) )
				{
					if( m_GameIsReliable && m_ActionReceived && m_ReconnectPort > 0 && GetTime( ) - m_RemoteSocket->GetLastRecv( ) >= 20 )
					{
						CONSOLE_Print( "[GPROXY] disconnected from remote server due to 20 second timeout" );
						SendLocalChat( "You have been timed out from the server." );
						uint32_t TimeRemaining = ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 - ( GetTime( ) - m_LastActionTime );

						if( GetTime( ) - m_LastActionTime > ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 )
							TimeRemaining = 0;

						SendLocalChat( "GProxy++ is attempting to reconnect... (" + UTIL_ToString( TimeRemaining ) + " seconds remain)" );
						CONSOLE_Print( "[GPROXY] attempting to reconnect" );
						m_RemoteSocket->Reset( );
						m_RemoteSocket->SetNoDelay( true );
						m_RemoteSocket->Connect( string( ), m_RemoteServerIP, m_ReconnectPort );
						m_LastConnectionAttemptTime = GetTime( );
					}
					else
					{
						m_RemoteSocket->DoRecv( &fd );
						ExtractRemotePackets( );
						ProcessRemotePackets( );

						if( m_GameIsReliable && m_ActionReceived && m_ReconnectPort > 0 && GetTime( ) - m_LastAckTime >= 10 )
						{
							m_RemoteSocket->PutBytes( m_GPSProtocol->SEND_GPSC_ACK( m_TotalPacketsReceivedFromRemote ) );
							m_LastAckTime = GetTime( );
						}

						m_RemoteSocket->DoSend( &send_fd );
					}
				}

				if( m_RemoteSocket->GetConnecting( ) )
				{
					// we are currently attempting to connect

					if( m_RemoteSocket->CheckConnect( ) )
					{
						// the connection attempt completed

						if( m_GameIsReliable && m_ActionReceived )
						{
							// this is a reconnection, not a new connection
							// if the server accepts the reconnect request it will send a GPS_RECONNECT back requesting a certain number of packets

							SendLocalChat( "GProxy++ reconnected to the server!" );
							SendLocalChat( "==================================================" );
							CONSOLE_Print( "[GPROXY] reconnected to remote server" );

							// note: even though we reset the socket when we were disconnected, we haven't been careful to ensure we never queued any data in the meantime
							// therefore it's possible the socket could have data in the send buffer
							// this is bad because the server will expect us to send a GPS_RECONNECT message first
							// so we must clear the send buffer before we continue
							// note: we aren't losing data here, any important messages that need to be sent have been put in the packet buffer
							// they will be requested by the server if required

							m_RemoteSocket->ClearSendBuffer( );
							m_RemoteSocket->PutBytes( m_GPSProtocol->SEND_GPSC_RECONNECT( m_PID, m_ReconnectKey, m_TotalPacketsReceivedFromRemote ) );

							// we cannot permit any forwarding of local packets until the game is synchronized again
							// this will disable forwarding and will be reset when the synchronization is complete

							m_Synchronized = false;
						}
						else
							CONSOLE_Print( "[GPROXY] connected to remote server" );
					}
					else if( GetTime( ) - m_LastConnectionAttemptTime >= 10 )
					{
						// the connection attempt timed out (10 seconds)

						CONSOLE_Print( "[GPROXY] connect to remote server timed out" );

						if( m_GameIsReliable && m_ActionReceived && m_ReconnectPort > 0 )
						{
							uint32_t TimeRemaining = ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 - ( GetTime( ) - m_LastActionTime );

							if( GetTime( ) - m_LastActionTime > ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 )
								TimeRemaining = 0;

							SendLocalChat( "GProxy++ is attempting to reconnect... (" + UTIL_ToString( TimeRemaining ) + " seconds remain)" );
							CONSOLE_Print( "[GPROXY] attempting to reconnect" );
							m_RemoteSocket->Reset( );
							m_RemoteSocket->SetNoDelay( true );
							m_RemoteSocket->Connect( string( ), m_RemoteServerIP, m_ReconnectPort );
							m_LastConnectionAttemptTime = GetTime( );
						}
						else
						{
							if( m_BNET->GetInGame( ) )
								m_BNET->QueueEnterChat( );

							m_LocalSocket->Disconnect( );
							delete m_LocalSocket;
							m_LocalSocket = NULL;
							m_RemoteSocket->Reset( );
							m_RemoteSocket->SetNoDelay( true );
							m_RemoteServerIP.clear( );
							m_RemoteServerPort = 0;
							return false;
						}
					}
				}
			}

			m_LocalSocket->DoSend( &send_fd );
		}
	}
	else
	{
		//
		// handle game listing
		//

		if( GetTime( ) - m_LastRefreshTime >= 2 )
		{
			for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); )
			{
				// expire games older than 60 seconds

				if( GetTime( ) - (*i)->GetReceivedTime( ) >= 60 )
				{
					// don't forget to remove it from the LAN list first

					m_UDPSocket->Broadcast( 6112, m_GameProtocol->SEND_W3GS_DECREATEGAME( (*i)->GetUniqueGameID( ) ) );
					delete *i;
					i = m_Games.erase( i );
					continue;
				}

				BYTEARRAY MapGameType;
				UTIL_AppendByteArray( MapGameType, (*i)->GetGameType( ), false );
				UTIL_AppendByteArray( MapGameType, (*i)->GetParameter( ), false );
				BYTEARRAY MapFlags = UTIL_CreateByteArray( (*i)->GetMapFlags( ), false );
				BYTEARRAY MapWidth = UTIL_CreateByteArray( (*i)->GetMapWidth( ), false );
				BYTEARRAY MapHeight = UTIL_CreateByteArray( (*i)->GetMapHeight( ), false );
				string GameName = (*i)->GetGameName( );

				// colour reliable game names so they're easier to pick out of the list

				if( (*i)->GetMapWidth( ) == 1984 && (*i)->GetMapHeight( ) == 1984 )
				{
					GameName = "|cFF4080C0" + GameName;

					// unfortunately we have to truncate them
					// is this acceptable?

					if( GameName.size( ) > 31 )
						GameName = GameName.substr( 0, 31 );
				}

				m_UDPSocket->Broadcast( 6112, m_GameProtocol->SEND_W3GS_GAMEINFO( m_TFT, m_War3Version, MapGameType, MapFlags, MapWidth, MapHeight, GameName, (*i)->GetHostName( ), (*i)->GetElapsedTime( ), (*i)->GetMapPath( ), (*i)->GetMapCRC( ), 12, 12, m_Port, (*i)->GetUniqueGameID( ), (*i)->GetUniqueGameID( ) ) );
				i++;
			}

			m_LastRefreshTime = GetTime( );
		}
	}

	return m_Exiting;
}

void CGProxy :: ExtractLocalPackets( )
{
	if( !m_LocalSocket )
		return;

	string *RecvBuffer = m_LocalSocket->GetBytes( );
	BYTEARRAY Bytes = UTIL_CreateByteArray( (unsigned char *)RecvBuffer->c_str( ), RecvBuffer->size( ) );

	// a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

	while( Bytes.size( ) >= 4 )
	{
		// byte 0 is always 247

		if( Bytes[0] == W3GS_HEADER_CONSTANT )
		{
			// bytes 2 and 3 contain the length of the packet

			uint16_t Length = UTIL_ByteArrayToUInt16( Bytes, false, 2 );

			if( Length >= 4 )
			{
				if( Bytes.size( ) >= Length )
				{
					// we have to do a little bit of packet processing here
					// this is because we don't want to forward any chat messages that start with a "/" as these may be forwarded to battle.net instead
					// in fact we want to pretend they were never even received from the proxy's perspective

					bool Forward = true;
					BYTEARRAY Data = BYTEARRAY( Bytes.begin( ), Bytes.begin( ) + Length );

					if( Bytes[1] == CGameProtocol :: W3GS_CHAT_TO_HOST )
					{
						if( Data.size( ) >= 5 )
						{
							unsigned int i = 5;
							unsigned char Total = Data[4];

							if( Total > 0 && Data.size( ) >= i + Total )
							{
								i += Total;
								unsigned char Flag = Data[i + 1];
								i += 2;

								string MessageString;

								if( Flag == 16 && Data.size( ) >= i + 1 )
								{
									BYTEARRAY Message = UTIL_ExtractCString( Data, i );
									MessageString = string( Message.begin( ), Message.end( ) );
								}
								else if( Flag == 32 && Data.size( ) >= i + 5 )
								{
									BYTEARRAY Message = UTIL_ExtractCString( Data, i + 4 );
									MessageString = string( Message.begin( ), Message.end( ) );
								}

								string Command = MessageString;
								transform( Command.begin( ), Command.end( ), Command.begin( ), (int(*)(int))tolower );

								if( Command.size( ) >= 1 && Command.substr( 0, 1 ) == "/" )
								{
									Forward = false;

									if( Command.size( ) >= 5 && Command.substr( 0, 4 ) == "/re " )
									{
										if( m_BNET->GetLoggedIn( ) )
										{
											if( !m_BNET->GetReplyTarget( ).empty( ) )
											{
												m_BNET->QueueChatCommand( MessageString.substr( 4 ), m_BNET->GetReplyTarget( ), true );
												SendLocalChat( "Whispered to " + m_BNET->GetReplyTarget( ) + ": " + MessageString.substr( 4 ) );
											}
											else
												SendLocalChat( "Nobody has whispered you yet." );
										}
										else
											SendLocalChat( "You are not connected to battle.net." );
									}
									else if( Command == "/sc" || Command == "/spoof" || Command == "/spoofcheck" || Command == "/spoof check" )
									{
										if( m_BNET->GetLoggedIn( ) )
										{
											if( !m_GameStarted )
											{
												m_BNET->QueueChatCommand( "spoofcheck", m_HostName, true );
												SendLocalChat( "Whispered to " + m_HostName + ": spoofcheck" );
											}
											else
												SendLocalChat( "The game has already started." );
										}
										else
											SendLocalChat( "You are not connected to battle.net." );
									}
									else if( Command == "/status" )
									{
										if( m_LocalSocket )
										{
											if( m_GameIsReliable && m_ReconnectPort > 0 )
												SendLocalChat( "GProxy++ disconnect protection: Enabled" );
											else
												SendLocalChat( "GProxy++ disconnect protection: Disabled" );

											if( m_BNET->GetLoggedIn( ) )
												SendLocalChat( "battle.net: Connected" );
											else
												SendLocalChat( "battle.net: Disconnected" );
										}
									}
									else if( Command.size( ) >= 4 && Command.substr( 0, 3 ) == "/w " )
									{
										if( m_BNET->GetLoggedIn( ) )
										{
											// todotodo: fix me

											m_BNET->QueueChatCommand( MessageString );
											// SendLocalChat( "Whispered to ???: ???" );
										}
										else
											SendLocalChat( "You are not connected to battle.net." );
									}
								}
							}
						}
					}

					if( Forward )
					{
						m_LocalPackets.push( new CCommandPacket( W3GS_HEADER_CONSTANT, Bytes[1], Data ) );
						m_PacketBuffer.push( new CCommandPacket( W3GS_HEADER_CONSTANT, Bytes[1], Data ) );
						m_TotalPacketsReceivedFromLocal++;
					}

					*RecvBuffer = RecvBuffer->substr( Length );
					Bytes = BYTEARRAY( Bytes.begin( ) + Length, Bytes.end( ) );
				}
				else
					return;
			}
			else
			{
				CONSOLE_Print( "[GPROXY] received invalid packet from local player (bad length)" );
				m_Exiting = true;
				return;
			}
		}
		else
		{
			CONSOLE_Print( "[GPROXY] received invalid packet from local player (bad header constant)" );
			m_Exiting = true;
			return;
		}
	}
}

void CGProxy :: ProcessLocalPackets( )
{
	if( !m_LocalSocket )
		return;

	while( !m_LocalPackets.empty( ) )
	{
		CCommandPacket *Packet = m_LocalPackets.front( );
		m_LocalPackets.pop( );
		BYTEARRAY Data = Packet->GetData( );

		if( Packet->GetPacketType( ) == W3GS_HEADER_CONSTANT )
		{
			if( Packet->GetID( ) == CGameProtocol :: W3GS_REQJOIN )
			{
				if( Data.size( ) >= 20 )
				{
					// parse

					uint32_t HostCounter = UTIL_ByteArrayToUInt32( Data, false, 4 );
					uint32_t EntryKey = UTIL_ByteArrayToUInt32( Data, false, 8 );
					unsigned char Unknown = Data[12];
					uint16_t ListenPort = UTIL_ByteArrayToUInt16( Data, false, 13 );
					uint32_t PeerKey = UTIL_ByteArrayToUInt32( Data, false, 15 );
					BYTEARRAY Name = UTIL_ExtractCString( Data, 19 );
					string NameString = string( Name.begin( ), Name.end( ) );
					BYTEARRAY Remainder = BYTEARRAY( Data.begin( ) + Name.size( ) + 20, Data.end( ) );

					if( Remainder.size( ) == 18 )
					{
						// lookup the game in the main list

						bool GameFound = false;

						for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); i++ )
						{
							if( (*i)->GetUniqueGameID( ) == EntryKey )
							{
								CONSOLE_Print( "[GPROXY] local player requested game name [" + (*i)->GetGameName( ) + "]" );

								if( NameString != m_Username )
									CONSOLE_Print( "[GPROXY] using battle.net name [" + m_Username + "] instead of requested name [" + NameString + "]" );

								CONSOLE_Print( "[GPROXY] connecting to remote server [" + (*i)->GetIPString( ) + "] on port " + UTIL_ToString( (*i)->GetPort( ) ) );
								m_RemoteServerIP = (*i)->GetIPString( );
								m_RemoteServerPort = (*i)->GetPort( );
								m_RemoteSocket->Reset( );
								m_RemoteSocket->SetNoDelay( true );
								m_RemoteSocket->Connect( string( ), m_RemoteServerIP, m_RemoteServerPort );
								m_LastConnectionAttemptTime = GetTime( );
								m_GameIsReliable = ( (*i)->GetMapWidth( ) == 1984 && (*i)->GetMapHeight( ) == 1984 );
								m_GameStarted = false;

								// rewrite packet

								BYTEARRAY DataRewritten;
								DataRewritten.push_back( W3GS_HEADER_CONSTANT );
								DataRewritten.push_back( Packet->GetID( ) );
								DataRewritten.push_back( 0 );
								DataRewritten.push_back( 0 );
								UTIL_AppendByteArray( DataRewritten, (*i)->GetHostCounter( ), false );
								UTIL_AppendByteArray( DataRewritten, (uint32_t)0, false );
								DataRewritten.push_back( Unknown );
								UTIL_AppendByteArray( DataRewritten, ListenPort, false );
								UTIL_AppendByteArray( DataRewritten, PeerKey, false );
								UTIL_AppendByteArray( DataRewritten, m_Username );
								UTIL_AppendByteArrayFast( DataRewritten, Remainder );
								BYTEARRAY LengthBytes;
								LengthBytes = UTIL_CreateByteArray( (uint16_t)DataRewritten.size( ), false );
								DataRewritten[2] = LengthBytes[0];
								DataRewritten[3] = LengthBytes[1];
								Data = DataRewritten;

								// tell battle.net we're joining a game (for automatic spoof checking)

								m_BNET->QueueJoinGame( (*i)->GetGameName( ) );

								// save the hostname for later (for manual spoof checking)

								m_JoinedName = NameString;
								m_HostName = (*i)->GetHostName( );
								GameFound = true;
								break;
							}
						}

						if( !GameFound )
						{
							CONSOLE_Print( "[GPROXY] local player requested unknown game (expired?)" );
							m_LocalSocket->Disconnect( );
						}
					}
					else
						CONSOLE_Print( "[GPROXY] received invalid join request from local player (invalid remainder)" );
				}
				else
					CONSOLE_Print( "[GPROXY] received invalid join request from local player (too short)" );
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_LEAVEGAME )
			{
				m_LeaveGameSent = true;
				m_LocalSocket->Disconnect( );
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_CHAT_TO_HOST )
			{
				// handled in ExtractLocalPackets (yes, it's ugly)
			}
		}

		// warning: do not forward any data if we are not synchronized (e.g. we are reconnecting and resynchronizing)
		// any data not forwarded here will be cached in the packet buffer and sent later so all is well

		if( m_RemoteSocket && m_Synchronized )
			m_RemoteSocket->PutBytes( Data );

		delete Packet;
	}
}

void CGProxy :: ExtractRemotePackets( )
{
	string *RecvBuffer = m_RemoteSocket->GetBytes( );
	BYTEARRAY Bytes = UTIL_CreateByteArray( (unsigned char *)RecvBuffer->c_str( ), RecvBuffer->size( ) );

	// a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

	while( Bytes.size( ) >= 4 )
	{
		if( Bytes[0] == W3GS_HEADER_CONSTANT || Bytes[0] == GPS_HEADER_CONSTANT )
		{
			// bytes 2 and 3 contain the length of the packet

			uint16_t Length = UTIL_ByteArrayToUInt16( Bytes, false, 2 );

			if( Length >= 4 )
			{
				if( Bytes.size( ) >= Length )
				{
					m_RemotePackets.push( new CCommandPacket( Bytes[0], Bytes[1], BYTEARRAY( Bytes.begin( ), Bytes.begin( ) + Length ) ) );

					if( Bytes[0] == W3GS_HEADER_CONSTANT )
						m_TotalPacketsReceivedFromRemote++;

					*RecvBuffer = RecvBuffer->substr( Length );
					Bytes = BYTEARRAY( Bytes.begin( ) + Length, Bytes.end( ) );
				}
				else
					return;
			}
			else
			{
				CONSOLE_Print( "[GPROXY] received invalid packet from remote server (bad length)" );
				m_Exiting = true;
				return;
			}
		}
		else
		{
			CONSOLE_Print( "[GPROXY] received invalid packet from remote server (bad header constant)" );
			m_Exiting = true;
			return;
		}
	}
}

void CGProxy :: ProcessRemotePackets( )
{
	if( !m_LocalSocket || !m_RemoteSocket )
		return;

	while( !m_RemotePackets.empty( ) )
	{
		CCommandPacket *Packet = m_RemotePackets.front( );
		m_RemotePackets.pop( );

		if( Packet->GetPacketType( ) == W3GS_HEADER_CONSTANT )
		{
			if( Packet->GetID( ) == CGameProtocol :: W3GS_SLOTINFOJOIN )
			{
				BYTEARRAY Data = Packet->GetData( );

				if( Data.size( ) >= 6 )
				{
					uint16_t SlotInfoSize = UTIL_ByteArrayToUInt16( Data, false, 4 );

					if( Data.size( ) >= 7 + SlotInfoSize )
						m_ChatPID = Data[6 + SlotInfoSize];
				}

				// send a GPS_INIT packet
				// if the server doesn't recognize it (e.g. it isn't GHost++) we should be kicked

				CONSOLE_Print( "[GPROXY] join request accepted by remote server" );

				if( m_GameIsReliable )
				{
					CONSOLE_Print( "[GPROXY] detected reliable game, starting GPS handshake" );
					m_RemoteSocket->PutBytes( m_GPSProtocol->SEND_GPSC_INIT( 1 ) );
				}
				else
					CONSOLE_Print( "[GPROXY] detected standard game, disconnect protection disabled" );
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_COUNTDOWN_END )
			{
				if( m_GameIsReliable && m_ReconnectPort > 0 )
					CONSOLE_Print( "[GPROXY] game started, disconnect protection enabled" );
				else
				{
					if( m_GameIsReliable )
						CONSOLE_Print( "[GPROXY] game started but GPS handshake not complete, disconnect protection disabled" );
					else
						CONSOLE_Print( "[GPROXY] game started" );
				}

				m_GameStarted = true;
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_INCOMING_ACTION )
			{
				if( m_GameIsReliable )
				{
					// we received a game update which means we can reset the number of empty actions we have to work with
					// we also must send any remaining empty actions now
					// note: the lag screen can't be up right now otherwise the server made a big mistake, so we don't need to check for it

					BYTEARRAY EmptyAction;
					EmptyAction.push_back( 0xF7 );
					EmptyAction.push_back( 0x0C );
					EmptyAction.push_back( 0x06 );
					EmptyAction.push_back( 0x00 );
					EmptyAction.push_back( 0x00 );
					EmptyAction.push_back( 0x00 );

					for( unsigned char i = m_NumEmptyActionsUsed; i < m_NumEmptyActions; i++ )
						m_LocalSocket->PutBytes( EmptyAction );

					m_NumEmptyActionsUsed = 0;
				}

				m_ActionReceived = true;
				m_LastActionTime = GetTime( );
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_START_LAG )
			{
				if( m_GameIsReliable )
				{
					BYTEARRAY Data = Packet->GetData( );

					if( Data.size( ) >= 5 )
					{
						unsigned char NumLaggers = Data[4];

						if( Data.size( ) == 5 + NumLaggers * 5 )
						{
							for( unsigned char i = 0; i < NumLaggers; i++ )
							{
								bool LaggerFound = false;

								for( vector<unsigned char> :: iterator j = m_Laggers.begin( ); j != m_Laggers.end( ); j++ )
								{
									if( *j == Data[5 + i * 5] )
										LaggerFound = true;
								}

								if( LaggerFound )
									CONSOLE_Print( "[GPROXY] warning - received start_lag on known lagger" );
								else
									m_Laggers.push_back( Data[5 + i * 5] );
							}
						}
						else
							CONSOLE_Print( "[GPROXY] warning - unhandled start_lag (2)" );
					}
					else
						CONSOLE_Print( "[GPROXY] warning - unhandled start_lag (1)" );
				}
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_STOP_LAG )
			{
				if( m_GameIsReliable )
				{
					BYTEARRAY Data = Packet->GetData( );

					if( Data.size( ) == 9 )
					{
						bool LaggerFound = false;

						for( vector<unsigned char> :: iterator i = m_Laggers.begin( ); i != m_Laggers.end( ); )
						{
							if( *i == Data[4] )
							{
								i = m_Laggers.erase( i );
								LaggerFound = true;
							}
							else
								i++;
						}

						if( !LaggerFound )
							CONSOLE_Print( "[GPROXY] warning - received stop_lag on unknown lagger" );
					}
					else
						CONSOLE_Print( "[GPROXY] warning - unhandled stop_lag" );
				}
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_INCOMING_ACTION2 )
			{
				if( m_GameIsReliable )
				{
					// we received a fractured game update which means we cannot use any empty actions until we receive the subsequent game update
					// we also must send any remaining empty actions now
					// note: this means if we get disconnected right now we can't use any of our buffer time, which would be very unlucky
					// it still gives us 60 seconds total to reconnect though
					// note: the lag screen can't be up right now otherwise the server made a big mistake, so we don't need to check for it

					BYTEARRAY EmptyAction;
					EmptyAction.push_back( 0xF7 );
					EmptyAction.push_back( 0x0C );
					EmptyAction.push_back( 0x06 );
					EmptyAction.push_back( 0x00 );
					EmptyAction.push_back( 0x00 );
					EmptyAction.push_back( 0x00 );

					for( unsigned char i = m_NumEmptyActionsUsed; i < m_NumEmptyActions; i++ )
						m_LocalSocket->PutBytes( EmptyAction );

					m_NumEmptyActionsUsed = m_NumEmptyActions;
				}
			}

			// forward the data

			m_LocalSocket->PutBytes( Packet->GetData( ) );

			// we have to wait until now to send the status message since otherwise the slotinfojoin itself wouldn't have been forwarded

			if( Packet->GetID( ) == CGameProtocol :: W3GS_SLOTINFOJOIN )
			{
				if( m_JoinedName != m_Username )
					SendLocalChat( "Using battle.net name \"" + m_Username + "\" instead of LAN name \"" + m_JoinedName + "\"." );

				if( m_GameIsReliable )
					SendLocalChat( "This is a reliable game. Requesting GProxy++ disconnect protection from server..." );
				else
					SendLocalChat( "This is an unreliable game. GProxy++ disconnect protection is disabled." );
			}
		}
		else if( Packet->GetPacketType( ) == GPS_HEADER_CONSTANT )
		{
			if( m_GameIsReliable )
			{
				BYTEARRAY Data = Packet->GetData( );

				if( Packet->GetID( ) == CGPSProtocol :: GPS_INIT && Data.size( ) == 12 )
				{
					m_ReconnectPort = UTIL_ByteArrayToUInt16( Data, false, 4 );
					m_PID = Data[6];
					m_ReconnectKey = UTIL_ByteArrayToUInt32( Data, false, 7 );
					m_NumEmptyActions = Data[11];
					SendLocalChat( "GProxy++ disconnect protection is ready (" + UTIL_ToString( ( m_NumEmptyActions + 1 ) * 60 ) + " second buffer)." );
					CONSOLE_Print( "[GPROXY] handshake complete, disconnect protection ready (" + UTIL_ToString( ( m_NumEmptyActions + 1 ) * 60 ) + " second buffer)" );
				}
				else if( Packet->GetID( ) == CGPSProtocol :: GPS_RECONNECT && Data.size( ) == 8 )
				{
					uint32_t LastPacket = UTIL_ByteArrayToUInt32( Data, false, 4 );
					uint32_t PacketsAlreadyUnqueued = m_TotalPacketsReceivedFromLocal - m_PacketBuffer.size( );

					if( LastPacket > PacketsAlreadyUnqueued )
					{
						uint32_t PacketsToUnqueue = LastPacket - PacketsAlreadyUnqueued;

						if( PacketsToUnqueue > m_PacketBuffer.size( ) )
						{
							CONSOLE_Print( "[GPROXY] received GPS_RECONNECT with last packet > total packets sent" );
							PacketsToUnqueue = m_PacketBuffer.size( );
						}

						while( PacketsToUnqueue > 0 )
						{
							delete m_PacketBuffer.front( );
							m_PacketBuffer.pop( );
							PacketsToUnqueue--;
						}
					}

					// send remaining packets from buffer, preserve buffer
					// note: any packets in m_LocalPackets are still sitting at the end of this buffer because they haven't been processed yet
					// therefore we must check for duplicates otherwise we might (will) cause a desync

					queue<CCommandPacket *> TempBuffer;

					while( !m_PacketBuffer.empty( ) )
					{
						if( m_PacketBuffer.size( ) > m_LocalPackets.size( ) )
							m_RemoteSocket->PutBytes( m_PacketBuffer.front( )->GetData( ) );

						TempBuffer.push( m_PacketBuffer.front( ) );
						m_PacketBuffer.pop( );
					}

					m_PacketBuffer = TempBuffer;

					// we can resume forwarding local packets again
					// doing so prior to this point could result in an out-of-order stream which would probably cause a desync

					m_Synchronized = true;
				}
				else if( Packet->GetID( ) == CGPSProtocol :: GPS_ACK && Data.size( ) == 8 )
				{
					uint32_t LastPacket = UTIL_ByteArrayToUInt32( Data, false, 4 );
					uint32_t PacketsAlreadyUnqueued = m_TotalPacketsReceivedFromLocal - m_PacketBuffer.size( );

					if( LastPacket > PacketsAlreadyUnqueued )
					{
						uint32_t PacketsToUnqueue = LastPacket - PacketsAlreadyUnqueued;

						if( PacketsToUnqueue > m_PacketBuffer.size( ) )
						{
							CONSOLE_Print( "[GPROXY] received GPS_ACK with last packet > total packets sent" );
							PacketsToUnqueue = m_PacketBuffer.size( );
						}

						while( PacketsToUnqueue > 0 )
						{
							delete m_PacketBuffer.front( );
							m_PacketBuffer.pop( );
							PacketsToUnqueue--;
						}
					}
				}
				else if( Packet->GetID( ) == CGPSProtocol :: GPS_REJECT && Data.size( ) == 8 )
				{
					uint32_t Reason = UTIL_ByteArrayToUInt32( Data, false, 4 );

					if( Reason == REJECTGPS_INVALID )
						CONSOLE_Print( "[GPROXY] rejected by remote server: invalid data" );
					else if( Reason == REJECTGPS_NOTFOUND )
						CONSOLE_Print( "[GPROXY] rejected by remote server: player not found in any running games" );

					m_LocalSocket->Disconnect( );
				}
			}
		}

		delete Packet;
	}
}

bool CGProxy :: AddGame( CIncomingGameHost *game )
{
	// check for duplicates and rehosted games

	bool DuplicateFound = false;
	uint32_t OldestReceivedTime = GetTime( );

	for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); i++ )
	{
		if( game->GetIP( ) == (*i)->GetIP( ) && game->GetPort( ) == (*i)->GetPort( ) )
		{
			// duplicate or rehosted game, delete the old one and add the new one
			// don't forget to remove the old one from the LAN list first

			m_UDPSocket->Broadcast( 6112, m_GameProtocol->SEND_W3GS_DECREATEGAME( (*i)->GetUniqueGameID( ) ) );
			delete *i;
			*i = game;
			DuplicateFound = true;
			break;
		}

		if( game->GetGameName( ) != m_BNET->GetSearchGameName( ) && game->GetReceivedTime( ) < OldestReceivedTime )
			OldestReceivedTime = game->GetReceivedTime( );
	}

	if( !DuplicateFound )
		m_Games.push_back( game );

	// the game list cannot hold more than 20 games (warcraft 3 doesn't handle it properly and ignores any further games)
	// if this game puts us over the limit, remove the oldest game
	// don't remove the "search game" since that's probably a pretty important game
	// note: it'll get removed automatically by the 60 second timeout in the main loop when appropriate

	if( m_Games.size( ) > 20 )
	{
		for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); i++ )
		{
			if( game->GetGameName( ) != m_BNET->GetSearchGameName( ) && game->GetReceivedTime( ) == OldestReceivedTime )
			{
				m_UDPSocket->Broadcast( 6112, m_GameProtocol->SEND_W3GS_DECREATEGAME( (*i)->GetUniqueGameID( ) ) );
				delete *i;
				m_Games.erase( i );
				break;
			}
		}
	}

	return !DuplicateFound;
}

void CGProxy :: SendLocalChat( string message )
{
	if( m_LocalSocket )
	{
		if( m_GameStarted )
		{
			if( message.size( ) > 127 )
				message = message.substr( 0, 127 );

			m_LocalSocket->PutBytes( m_GameProtocol->SEND_W3GS_CHAT_FROM_HOST( m_ChatPID, UTIL_CreateByteArray( m_ChatPID ), 32, UTIL_CreateByteArray( (uint32_t)0, false ), message ) );
		}
		else
		{
			if( message.size( ) > 254 )
				message = message.substr( 0, 254 );

			m_LocalSocket->PutBytes( m_GameProtocol->SEND_W3GS_CHAT_FROM_HOST( m_ChatPID, UTIL_CreateByteArray( m_ChatPID ), 16, BYTEARRAY( ), message ) );
		}
	}
}

void CGProxy :: SendEmptyAction( )
{
	// we can't send any empty actions while the lag screen is up
	// so we keep track of who the lag screen is currently showing (if anyone) and we tear it down, send the empty action, and put it back up

	for( vector<unsigned char> :: iterator i = m_Laggers.begin( ); i != m_Laggers.end( ); i++ )
	{
		BYTEARRAY StopLag;
		StopLag.push_back( 0xF7 );
		StopLag.push_back( 0x11 );
		StopLag.push_back( 0x09 );
		StopLag.push_back( 0 );
		StopLag.push_back( *i );
		UTIL_AppendByteArray( StopLag, (uint32_t)60000, false );
		m_LocalSocket->PutBytes( StopLag );
	}

	BYTEARRAY EmptyAction;
	EmptyAction.push_back( 0xF7 );
	EmptyAction.push_back( 0x0C );
	EmptyAction.push_back( 0x06 );
	EmptyAction.push_back( 0x00 );
	EmptyAction.push_back( 0x00 );
	EmptyAction.push_back( 0x00 );
	m_LocalSocket->PutBytes( EmptyAction );

	if( !m_Laggers.empty( ) )
	{
		BYTEARRAY StartLag;
		StartLag.push_back( 0xF7 );
		StartLag.push_back( 0x10 );
		StartLag.push_back( 0 );
		StartLag.push_back( 0 );
		StartLag.push_back( m_Laggers.size( ) );

		for( vector<unsigned char> :: iterator i = m_Laggers.begin( ); i != m_Laggers.end( ); i++ )
		{
			// using a lag time of 60000 ms means the counter will start at zero
			// hopefully warcraft 3 doesn't care about wild variations in the lag time in subsequent packets

			StartLag.push_back( *i );
			UTIL_AppendByteArray( StartLag, (uint32_t)60000, false );
		}

		BYTEARRAY LengthBytes;
		LengthBytes = UTIL_CreateByteArray( (uint16_t)StartLag.size( ), false );
		StartLag[2] = LengthBytes[0];
		StartLag[3] = LengthBytes[1];
		m_LocalSocket->PutBytes( StartLag );
	}
}
