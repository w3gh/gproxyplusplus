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

#include "gproxy.h"
#include "util.h"
#include "config.h"
#include "socket.h"
#include "commandpacket.h"
#include "bncsutilinterface.h"
// #include "bnlsclient.h"
#include "bnetprotocol.h"
#include "bnet.h"
#include "gameprotocol.h"

//
// CBNET
//

CBNET :: CBNET( CGProxy *nGProxy, string nServer, string nBNLSServer, uint16_t nBNLSPort, uint32_t nBNLSWardenCookie, string nCDKeyROC, string nCDKeyTFT, string nCountryAbbrev, string nCountry, string nUserName, string nUserPassword, string nFirstChannel, unsigned char nWar3Version, BYTEARRAY nEXEVersion, BYTEARRAY nEXEVersionHash, string nPasswordHashType, uint32_t nMaxMessageLength )
{
	// todotodo: append path seperator to Warcraft3Path if needed

	m_GProxy = nGProxy;
	m_Socket = new CTCPClient( );
	m_Protocol = new CBNETProtocol( );
	// m_BNLSClient = NULL;
	m_BNCSUtil = new CBNCSUtilInterface( nUserName, nUserPassword );
	m_Server = nServer;

	if( nPasswordHashType == "pvpgn" && !nBNLSServer.empty( ) )
	{
		CONSOLE_Print( "[BNET] pvpgn connection found with a configured BNLS server, ignoring BNLS server" );
		nBNLSServer.clear( );
		nBNLSPort = 0;
		nBNLSWardenCookie = 0;
	}

	m_BNLSServer = nBNLSServer;
	m_BNLSPort = nBNLSPort;
	m_BNLSWardenCookie = nBNLSWardenCookie;
	m_CDKeyROC = nCDKeyROC;
	m_CDKeyTFT = nCDKeyTFT;

	// remove dashes from CD keys and convert to uppercase

	m_CDKeyROC.erase( remove( m_CDKeyROC.begin( ), m_CDKeyROC.end( ), '-' ), m_CDKeyROC.end( ) );
	m_CDKeyTFT.erase( remove( m_CDKeyTFT.begin( ), m_CDKeyTFT.end( ), '-' ), m_CDKeyTFT.end( ) );
	transform( m_CDKeyROC.begin( ), m_CDKeyROC.end( ), m_CDKeyROC.begin( ), (int(*)(int))toupper );
	transform( m_CDKeyTFT.begin( ), m_CDKeyTFT.end( ), m_CDKeyTFT.begin( ), (int(*)(int))toupper );

	if( m_CDKeyROC.size( ) != 26 )
		CONSOLE_Print( "[BNET] warning - your ROC CD key is not 26 characters long and is probably invalid" );

	if( m_GProxy->m_TFT && m_CDKeyTFT.size( ) != 26 )
		CONSOLE_Print( "[BNET] warning - your TFT CD key is not 26 characters long and is probably invalid" );

	m_CountryAbbrev = nCountryAbbrev;
	m_Country = nCountry;
	m_UserName = nUserName;
	m_UserPassword = nUserPassword;
	m_FirstChannel = nFirstChannel;
	m_ListPublicGames = false;
	m_SearchGameNameTime = 0;
	m_War3Version = nWar3Version;
	m_EXEVersion = nEXEVersion;
	m_EXEVersionHash = nEXEVersionHash;
	m_PasswordHashType = nPasswordHashType;
	m_MaxMessageLength = nMaxMessageLength;
	m_LastDisconnectedTime = 0;
	m_LastConnectionAttemptTime = 0;
	m_LastNullTime = 0;
	m_LastOutPacketTicks = 0;
	m_LastOutPacketSize = 0;
	m_LastGetPublicListTime = 0;
	m_LastGetSearchGameTime = 0;
	m_FirstConnect = true;
	m_WaitingToConnect = true;
	m_LoggedIn = false;
	m_InChat = false;
	m_InGame = false;
}

CBNET :: ~CBNET( )
{
	delete m_Socket;
	delete m_Protocol;
	// delete m_BNLSClient;

	while( !m_Packets.empty( ) )
	{
		delete m_Packets.front( );
		m_Packets.pop( );
	}

	delete m_BNCSUtil;
}

BYTEARRAY CBNET :: GetUniqueName( )
{
	return m_Protocol->GetUniqueName( );
}

unsigned int CBNET :: SetFD( void *fd, void *send_fd, int *nfds )
{
	unsigned int NumFDs = 0;

	if( !m_Socket->HasError( ) && m_Socket->GetConnected( ) )
	{
		m_Socket->SetFD( (fd_set *)fd, (fd_set *)send_fd, nfds );
		NumFDs++;

		/* if( m_BNLSClient )
			NumFDs += m_BNLSClient->SetFD( fd, send_fd, nfds ); */
	}

	return NumFDs;
}

bool CBNET :: Update( void *fd, void *send_fd )
{
	// we return at the end of each if statement so we don't have to deal with errors related to the order of the if statements
	// that means it might take a few ms longer to complete a task involving multiple steps (in this case, reconnecting) due to blocking or sleeping
	// but it's not a big deal at all, maybe 100ms in the worst possible case (based on a 50ms blocking time)

	if( m_Socket->HasError( ) )
	{
		// the socket has an error

		CONSOLE_Print( "[BNET] disconnected from battle.net due to socket error" );

		if( m_Socket->GetError( ) == ECONNRESET && GetTime( ) - m_LastConnectionAttemptTime <= 15 )
			CONSOLE_Print( "[BNET] warning - you are probably temporarily IP banned from battle.net" );
		else
		{
			if( m_GProxy->m_LocalSocket )
				m_GProxy->SendLocalChat( "Disconnected from battle.net." );
		}

		CONSOLE_Print( "[BNET] waiting 90 seconds to reconnect" );
		/* delete m_BNLSClient;
		m_BNLSClient = NULL; */
		m_BNCSUtil->Reset( m_UserName, m_UserPassword );
		m_Socket->Reset( );
		m_LastDisconnectedTime = GetTime( );
		m_LoggedIn = false;
		m_InChat = false;
		m_InGame = false;
		m_WaitingToConnect = true;
		return false;
	}

	if( !m_Socket->GetConnecting( ) && !m_Socket->GetConnected( ) && !m_WaitingToConnect )
	{
		// the socket was disconnected

		CONSOLE_Print( "[BNET] disconnected from battle.net" );

		if( m_GProxy->m_LocalSocket )
			m_GProxy->SendLocalChat( "Disconnected from battle.net." );

		CONSOLE_Print( "[BNET] waiting 90 seconds to reconnect" );
		/* delete m_BNLSClient;
		m_BNLSClient = NULL; */
		m_BNCSUtil->Reset( m_UserName, m_UserPassword );
		m_Socket->Reset( );
		m_LastDisconnectedTime = GetTime( );
		m_LoggedIn = false;
		m_InChat = false;
		m_InGame = false;
		m_WaitingToConnect = true;
		return false;
	}

	if( m_Socket->GetConnected( ) )
	{
		// the socket is connected and everything appears to be working properly

		m_Socket->DoRecv( (fd_set *)fd );
		ExtractPackets( );
		ProcessPackets( );

		// update the BNLS client

		/*

		if( m_BNLSClient )
		{
			if( m_BNLSClient->Update( fd, send_fd ) )
			{
				CONSOLE_Print( "[BNET] deleting BNLS client" );
				delete m_BNLSClient;
				m_BNLSClient = NULL;
			}
			else
			{
				BYTEARRAY WardenResponse = m_BNLSClient->GetWardenResponse( );

				if( !WardenResponse.empty( ) )
					m_Socket->PutBytes( m_Protocol->SEND_SID_WARDEN( WardenResponse ) );
			}
		}

		*/

		// request the public game list every 15 seconds

		if( !m_GProxy->m_LocalSocket && m_ListPublicGames && GetTime( ) - m_LastGetPublicListTime >= 15 && m_OutPackets.size( ) <= 2 )
		{
			// request 20 games (note: it seems like 20 is the maximum, requesting more doesn't result in more results returned)

			QueueGetGameList( 20 );
			m_LastGetPublicListTime = GetTime( );
		}

		// request the search game every 15 seconds

		if( !m_SearchGameName.empty( ) && GetTime( ) - m_SearchGameNameTime >= 120 )
		{
			CONSOLE_Print( "[BNET] stopped searching for game \"" + m_SearchGameName + "\"" );
			m_SearchGameName.clear( );
			m_SearchGameNameTime = GetTime( );
		}

		if( !m_GProxy->m_LocalSocket && !m_SearchGameName.empty( ) && GetTime( ) - m_LastGetSearchGameTime >= 15 && m_OutPackets.size( ) <= 2 )
		{
			QueueGetGameList( m_SearchGameName );
			m_LastGetSearchGameTime = GetTime( );
		}

		// check if at least one packet is waiting to be sent and if we've waited long enough to prevent flooding
		// this formula has changed many times but currently we wait 1 second if the last packet was "small", 3.5 seconds if it was "medium", and 4 seconds if it was "big"

		uint32_t WaitTicks = 0;

		if( m_LastOutPacketSize < 10 )
			WaitTicks = 1000;
		else if( m_LastOutPacketSize < 100 )
			WaitTicks = 3500;
		else
			WaitTicks = 4000;

		if( !m_OutPackets.empty( ) && GetTicks( ) - m_LastOutPacketTicks >= WaitTicks )
		{
			if( m_OutPackets.size( ) > 7 )
				CONSOLE_Print( "[BNET] packet queue warning - there are " + UTIL_ToString( m_OutPackets.size( ) ) + " packets waiting to be sent" );

			m_Socket->PutBytes( m_OutPackets.front( ) );
			m_LastOutPacketSize = m_OutPackets.front( ).size( );
			m_OutPackets.pop( );
			m_LastOutPacketTicks = GetTicks( );
		}

		// send a null packet every 60 seconds to detect disconnects

		if( GetTime( ) - m_LastNullTime >= 60 && GetTicks( ) - m_LastOutPacketTicks >= 60000 )
		{
			m_Socket->PutBytes( m_Protocol->SEND_SID_NULL( ) );
			m_LastNullTime = GetTime( );
		}

		m_Socket->DoSend( (fd_set *)send_fd );
		return false;
	}

	if( m_Socket->GetConnecting( ) )
	{
		// we are currently attempting to connect to battle.net

		if( m_Socket->CheckConnect( ) )
		{
			// the connection attempt completed

			CONSOLE_Print( "[BNET] connected" );
			m_Socket->PutBytes( m_Protocol->SEND_PROTOCOL_INITIALIZE_SELECTOR( ) );
			m_Socket->PutBytes( m_Protocol->SEND_SID_AUTH_INFO( m_War3Version, m_GProxy->m_TFT, m_CountryAbbrev, m_Country ) );
			m_Socket->DoSend( (fd_set *)send_fd );
			m_LastNullTime = GetTime( );
			m_LastOutPacketTicks = GetTicks( );

			while( !m_OutPackets.empty( ) )
				m_OutPackets.pop( );

			return false;
		}
		else if( GetTime( ) - m_LastConnectionAttemptTime >= 15 )
		{
			// the connection attempt timed out (15 seconds)

			CONSOLE_Print( "[BNET] connect timed out" );
			CONSOLE_Print( "[BNET] waiting 90 seconds to reconnect" );
			m_Socket->Reset( );
			m_LastDisconnectedTime = GetTime( );
			m_WaitingToConnect = true;
			return false;
		}
	}

	if( !m_Socket->GetConnecting( ) && !m_Socket->GetConnected( ) && ( m_FirstConnect || GetTime( ) - m_LastDisconnectedTime >= 90 ) )
	{
		// attempt to connect to battle.net

		m_FirstConnect = false;
		CONSOLE_Print( "[BNET] connecting to server [" + m_Server + "] on port 6112" );

		if( m_ServerIP.empty( ) )
		{
			m_Socket->Connect( "", m_Server, 6112 );

			if( !m_Socket->HasError( ) )
			{
				m_ServerIP = m_Socket->GetIPString( );
				CONSOLE_Print( "[BNET] resolved and cached server IP address " + m_ServerIP );
			}
		}
		else
		{
			// use cached server IP address since resolving takes time and is blocking

			CONSOLE_Print( "[BNET] using cached server IP address " + m_ServerIP );
			m_Socket->Connect( "", m_ServerIP, 6112 );
		}

		m_WaitingToConnect = false;
		m_LastConnectionAttemptTime = GetTime( );
		return false;
	}

	return false;
}

void CBNET :: ExtractPackets( )
{
	// extract as many packets as possible from the socket's receive buffer and put them in the m_Packets queue

	string *RecvBuffer = m_Socket->GetBytes( );
	BYTEARRAY Bytes = UTIL_CreateByteArray( (unsigned char *)RecvBuffer->c_str( ), RecvBuffer->size( ) );

	// a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

	while( Bytes.size( ) >= 4 )
	{
		// byte 0 is always 255

		if( Bytes[0] == BNET_HEADER_CONSTANT )
		{
			// bytes 2 and 3 contain the length of the packet

			uint16_t Length = UTIL_ByteArrayToUInt16( Bytes, false, 2 );

			if( Length >= 4 )
			{
				if( Bytes.size( ) >= Length )
				{
					m_Packets.push( new CCommandPacket( BNET_HEADER_CONSTANT, Bytes[1], BYTEARRAY( Bytes.begin( ), Bytes.begin( ) + Length ) ) );
					*RecvBuffer = RecvBuffer->substr( Length );
					Bytes = BYTEARRAY( Bytes.begin( ) + Length, Bytes.end( ) );
				}
				else
					return;
			}
			else
			{
				CONSOLE_Print( "[BNET] error - received invalid packet from battle.net (bad length), disconnecting" );
				m_Socket->Disconnect( );
				return;
			}
		}
		else
		{
			CONSOLE_Print( "[BNET] error - received invalid packet from battle.net (bad header constant), disconnecting" );
			m_Socket->Disconnect( );
			return;
		}
	}
}

void CBNET :: ProcessPackets( )
{
	vector<CIncomingGameHost *> Games;
	uint32_t GamesReceived = 0;
	uint32_t OldReliableGamesReceived = 0;
	uint32_t NewReliableGamesReceived = 0;
	CIncomingChatEvent *ChatEvent = NULL;
	BYTEARRAY WardenData;
	vector<CIncomingFriendList *> Friends;
	vector<CIncomingClanList *> Clans;

	// process all the received packets in the m_Packets queue
	// this normally means sending some kind of response

	while( !m_Packets.empty( ) )
	{
		CCommandPacket *Packet = m_Packets.front( );
		m_Packets.pop( );

		if( Packet->GetPacketType( ) == BNET_HEADER_CONSTANT )
		{
			switch( Packet->GetID( ) )
			{
			case CBNETProtocol :: SID_NULL:
				// warning: we do not respond to NULL packets with a NULL packet of our own
				// this is because PVPGN servers are programmed to respond to NULL packets so it will create a vicious cycle of useless traffic
				// official battle.net servers do not respond to NULL packets

				m_Protocol->RECEIVE_SID_NULL( Packet->GetData( ) );
				break;

			case CBNETProtocol :: SID_GETADVLISTEX:
				Games = m_Protocol->RECEIVE_SID_GETADVLISTEX( Packet->GetData( ) );

				// check for reliable games
				// GHost++ uses specific invalid map dimensions (1984) to indicated reliable games

				GamesReceived = Games.size( );

				for( vector<CIncomingGameHost *> :: iterator i = Games.begin( ); i != Games.end( ); )
				{
					/* if( (*i)->GetMapWidth( ) != 1984 || (*i)->GetMapHeight( ) != 1984 )
					{
						// not a reliable game

						delete *i;
						i = Games.erase( i );
						continue;
					} */

					// filter game names

					if( !m_PublicGameFilter.empty( ) && (*i)->GetGameName( ) != m_SearchGameName )
					{
						string FilterLower = m_PublicGameFilter;
						string GameNameLower = (*i)->GetGameName( );
						transform( FilterLower.begin( ), FilterLower.end( ), FilterLower.begin( ), (int(*)(int))tolower );
						transform( GameNameLower.begin( ), GameNameLower.end( ), GameNameLower.begin( ), (int(*)(int))tolower );

						if( GameNameLower.find( FilterLower ) == string :: npos )
						{
							delete *i;
							i = Games.erase( i );
							continue;
						}
					}

					if( m_GProxy->AddGame( *i ) )
						NewReliableGamesReceived++;
					else
						OldReliableGamesReceived++;

					i++;
				}

				/* if( GamesReceived > 0 )
					CONSOLE_Print( "[BNET] sifted game list, found " + UTIL_ToString( OldReliableGamesReceived + NewReliableGamesReceived ) + "/" + UTIL_ToString( GamesReceived ) + " reliable games (" + UTIL_ToString( OldReliableGamesReceived ) + " duplicates)" ); */

				break;

			case CBNETProtocol :: SID_ENTERCHAT:
				if( m_Protocol->RECEIVE_SID_ENTERCHAT( Packet->GetData( ) ) )
				{
					CONSOLE_Print( "[BNET] joining channel [" + m_FirstChannel + "]" );
					m_InChat = true;
					m_InGame = false;
					m_Socket->PutBytes( m_Protocol->SEND_SID_JOINCHANNEL( m_FirstChannel ) );
				}

				break;

			case CBNETProtocol :: SID_CHATEVENT:
				ChatEvent = m_Protocol->RECEIVE_SID_CHATEVENT( Packet->GetData( ) );

				if( ChatEvent )
					ProcessChatEvent( ChatEvent );

				delete ChatEvent;
				ChatEvent = NULL;
				break;

			case CBNETProtocol :: SID_CHECKAD:
				m_Protocol->RECEIVE_SID_CHECKAD( Packet->GetData( ) );
				break;

			case CBNETProtocol :: SID_STARTADVEX3:
				if( m_Protocol->RECEIVE_SID_STARTADVEX3( Packet->GetData( ) ) )
				{
					m_InChat = false;
					m_InGame = true;
				}
				else
					CONSOLE_Print( "[BNET] startadvex3 failed" );

				break;

			case CBNETProtocol :: SID_PING:
				m_Socket->PutBytes( m_Protocol->SEND_SID_PING( m_Protocol->RECEIVE_SID_PING( Packet->GetData( ) ) ) );
				break;

			case CBNETProtocol :: SID_AUTH_INFO:
				if( m_Protocol->RECEIVE_SID_AUTH_INFO( Packet->GetData( ) ) )
				{
					if( m_BNCSUtil->HELP_SID_AUTH_CHECK( m_GProxy->m_TFT, m_GProxy->m_War3Path, m_CDKeyROC, m_CDKeyTFT, m_Protocol->GetValueStringFormulaString( ), m_Protocol->GetIX86VerFileNameString( ), m_Protocol->GetClientToken( ), m_Protocol->GetServerToken( ) ) )
					{
						// override the exe information generated by bncsutil if specified in the config file
						// apparently this is useful for pvpgn users

						if( m_EXEVersion.size( ) == 4 )
						{
							CONSOLE_Print( "[BNET] using custom exe version bnet_custom_exeversion = " + UTIL_ToString( m_EXEVersion[0] ) + " " + UTIL_ToString( m_EXEVersion[1] ) + " " + UTIL_ToString( m_EXEVersion[2] ) + " " + UTIL_ToString( m_EXEVersion[3] ) );
							m_BNCSUtil->SetEXEVersion( m_EXEVersion );
						}

						if( m_EXEVersionHash.size( ) == 4 )
						{
							CONSOLE_Print( "[BNET] using custom exe version hash bnet_custom_exeversionhash = " + UTIL_ToString( m_EXEVersionHash[0] ) + " " + UTIL_ToString( m_EXEVersionHash[1] ) + " " + UTIL_ToString( m_EXEVersionHash[2] ) + " " + UTIL_ToString( m_EXEVersionHash[3] ) );
							m_BNCSUtil->SetEXEVersionHash( m_EXEVersionHash );
						}

						if( m_GProxy->m_TFT )
							CONSOLE_Print( "[BNET] attempting to auth as Warcraft III: The Frozen Throne" );
						else
							CONSOLE_Print( "[BNET] attempting to auth as Warcraft III: Reign of Chaos" );							

						m_Socket->PutBytes( m_Protocol->SEND_SID_AUTH_CHECK( m_GProxy->m_TFT, m_Protocol->GetClientToken( ), m_BNCSUtil->GetEXEVersion( ), m_BNCSUtil->GetEXEVersionHash( ), m_BNCSUtil->GetKeyInfoROC( ), m_BNCSUtil->GetKeyInfoTFT( ), m_BNCSUtil->GetEXEInfo( ), "GProxy" ) );

						// the Warden seed is the first 4 bytes of the ROC key hash
						// initialize the Warden handler

						/*

						if( !m_BNLSServer.empty( ) )
						{
							CONSOLE_Print( "[BNET] creating BNLS client" );
							delete m_BNLSClient;
							m_BNLSClient = new CBNLSClient( m_BNLSServer, m_BNLSPort, m_BNLSWardenCookie );
							m_BNLSClient->QueueWardenSeed( UTIL_ByteArrayToUInt32( m_BNCSUtil->GetKeyInfoROC( ), false, 16 ) );
						}

						*/
					}
					else
					{
						CONSOLE_Print( "[BNET] logon failed - bncsutil key hash failed (check your Warcraft 3 path and cd keys), disconnecting" );
						m_Socket->Disconnect( );
						delete Packet;
						return;
					}
				}

				break;

			case CBNETProtocol :: SID_AUTH_CHECK:
				if( m_Protocol->RECEIVE_SID_AUTH_CHECK( Packet->GetData( ) ) )
				{
					// cd keys accepted

					CONSOLE_Print( "[BNET] cd keys accepted" );
					m_BNCSUtil->HELP_SID_AUTH_ACCOUNTLOGON( );
					m_Socket->PutBytes( m_Protocol->SEND_SID_AUTH_ACCOUNTLOGON( m_BNCSUtil->GetClientKey( ), m_UserName ) );
				}
				else
				{
					// cd keys not accepted

					switch( UTIL_ByteArrayToUInt32( m_Protocol->GetKeyState( ), false ) )
					{
					case CBNETProtocol :: KR_ROC_KEY_IN_USE:
						CONSOLE_Print( "[BNET] logon failed - ROC CD key in use by user [" + m_Protocol->GetKeyStateDescription( ) + "], disconnecting" );
						break;
					case CBNETProtocol :: KR_TFT_KEY_IN_USE:
						CONSOLE_Print( "[BNET] logon failed - TFT CD key in use by user [" + m_Protocol->GetKeyStateDescription( ) + "], disconnecting" );
						break;
					case CBNETProtocol :: KR_OLD_GAME_VERSION:
						CONSOLE_Print( "[BNET] logon failed - game version is too old, disconnecting" );
						break;
					case CBNETProtocol :: KR_INVALID_VERSION:
						CONSOLE_Print( "[BNET] logon failed - game version is invalid, disconnecting" );
						break;
					default:
						CONSOLE_Print( "[BNET] logon failed - cd keys not accepted, disconnecting" );
						break;
					}

					m_Socket->Disconnect( );
					delete Packet;
					return;
				}

				break;

			case CBNETProtocol :: SID_AUTH_ACCOUNTLOGON:
				if( m_Protocol->RECEIVE_SID_AUTH_ACCOUNTLOGON( Packet->GetData( ) ) )
				{
					CONSOLE_Print( "[BNET] username [" + m_UserName + "] accepted" );

					if( m_PasswordHashType == "pvpgn" )
					{
						// pvpgn logon

						CONSOLE_Print( "[BNET] using pvpgn logon type (for pvpgn servers only)" );
						m_BNCSUtil->HELP_PvPGNPasswordHash( m_UserPassword );
						m_Socket->PutBytes( m_Protocol->SEND_SID_AUTH_ACCOUNTLOGONPROOF( m_BNCSUtil->GetPvPGNPasswordHash( ) ) );
					}
					else
					{
						// battle.net logon

						CONSOLE_Print( "[BNET] using battle.net logon type (for official battle.net servers only)" );
						m_BNCSUtil->HELP_SID_AUTH_ACCOUNTLOGONPROOF( m_Protocol->GetSalt( ), m_Protocol->GetServerPublicKey( ) );
						m_Socket->PutBytes( m_Protocol->SEND_SID_AUTH_ACCOUNTLOGONPROOF( m_BNCSUtil->GetM1( ) ) );
					}
				}
				else
				{
					CONSOLE_Print( "[BNET] logon failed - invalid username, disconnecting" );
					m_Socket->Disconnect( );
					delete Packet;
					return;
				}

				break;

			case CBNETProtocol :: SID_AUTH_ACCOUNTLOGONPROOF:
				if( m_Protocol->RECEIVE_SID_AUTH_ACCOUNTLOGONPROOF( Packet->GetData( ) ) )
				{
					// logon successful

					CONSOLE_Print( "[BNET] logon successful" );
					m_LoggedIn = true;
					m_Socket->PutBytes( m_Protocol->SEND_SID_NETGAMEPORT( 6112 ) );
					m_Socket->PutBytes( m_Protocol->SEND_SID_ENTERCHAT( ) );

					if( m_GProxy->m_LocalSocket )
						m_GProxy->SendLocalChat( "Connected to battle.net." );
				}
				else
				{
					CONSOLE_Print( "[BNET] logon failed - invalid password, disconnecting" );

					// try to figure out if the user might be using the wrong logon type since too many people are confused by this

					string Server = m_Server;
					transform( Server.begin( ), Server.end( ), Server.begin( ), (int(*)(int))tolower );

					if( m_PasswordHashType == "pvpgn" && ( Server == "useast.battle.net" || Server == "uswest.battle.net" || Server == "asia.battle.net" || Server == "europe.battle.net" ) )
						CONSOLE_Print( "[BNET] it looks like you're trying to connect to a battle.net server using a pvpgn logon type, check your config file's \"battle.net custom data\" section" );
					else if( m_PasswordHashType != "pvpgn" && ( Server != "useast.battle.net" && Server != "uswest.battle.net" && Server != "asia.battle.net" && Server != "europe.battle.net" ) )
						CONSOLE_Print( "[BNET] it looks like you're trying to connect to a pvpgn server using a battle.net logon type, check your config file's \"battle.net custom data\" section" );

					m_Socket->Disconnect( );
					delete Packet;
					return;
				}

				break;

			case CBNETProtocol :: SID_WARDEN:
				WardenData = m_Protocol->RECEIVE_SID_WARDEN( Packet->GetData( ) );

				/* if( m_BNLSClient )
					m_BNLSClient->QueueWardenRaw( WardenData );
				else */
					CONSOLE_Print( "[BNET] warning - received warden packet but no BNLS server is available, you will be kicked from battle.net soon" );

				break;

			case CBNETProtocol :: SID_FRIENDSLIST:
				break;

			case CBNETProtocol :: SID_CLANMEMBERLIST:
				break;
			}
		}

		delete Packet;
	}
}

void CBNET :: ProcessChatEvent( CIncomingChatEvent *chatEvent )
{
	CBNETProtocol :: IncomingChatEvent Event = chatEvent->GetChatEvent( );
	bool Whisper = ( Event == CBNETProtocol :: EID_WHISPER );
	string User = chatEvent->GetUser( );
	string Message = chatEvent->GetMessage( );

	if( Event == CBNETProtocol :: EID_SHOWUSER )
		CONSOLE_AddChannelUser( User );
	else if( Event == CBNETProtocol :: EID_JOIN )
		CONSOLE_AddChannelUser( User );
	else if( Event == CBNETProtocol :: EID_LEAVE )
		CONSOLE_RemoveChannelUser( User );
	else if( Event == CBNETProtocol :: EID_WHISPER )
	{
		m_ReplyTarget = User;
		CONSOLE_Print( "[WHISPER] [" + User + "] " + Message );

		if( m_GProxy->m_LocalSocket )
			m_GProxy->SendLocalChat( User + " whispers: " + Message );
	}
	else if( Event == CBNETProtocol :: EID_TALK )
		CONSOLE_Print( "[LOCAL] [" + User + "] " + Message );
	else if( Event == CBNETProtocol :: EID_BROADCAST )
		CONSOLE_Print( "[BROADCAST] " + Message );
	else if( Event == CBNETProtocol :: EID_CHANNEL )
	{
		CONSOLE_Print( "[BNET] joined channel [" + Message + "]" );
		m_CurrentChannel = Message;
		CONSOLE_ChangeChannel( Message );
		CONSOLE_RemoveChannelUsers( );
		CONSOLE_AddChannelUser( m_UserName );
	}
	else if( Event == CBNETProtocol :: EID_CHANNELFULL )
		CONSOLE_Print( "[BNET] channel is full" );
	else if( Event == CBNETProtocol :: EID_CHANNELDOESNOTEXIST )
		CONSOLE_Print( "[BNET] channel does not exist" );
	else if( Event == CBNETProtocol :: EID_CHANNELRESTRICTED )
		CONSOLE_Print( "[BNET] channel restricted" );
	else if( Event == CBNETProtocol :: EID_INFO )
		CONSOLE_Print( "[INFO] " + Message );
	else if( Event == CBNETProtocol :: EID_ERROR )
		CONSOLE_Print( "[ERROR] " + Message );
	else if( Event == CBNETProtocol :: EID_EMOTE )
		CONSOLE_Print( "[EMOTE] [" + User + "] " + Message );
}

void CBNET :: SendJoinChannel( string channel )
{
	if( m_LoggedIn && m_InChat )
		m_Socket->PutBytes( m_Protocol->SEND_SID_JOINCHANNEL( channel ) );
}

void CBNET :: QueueEnterChat( )
{
	if( m_LoggedIn )
		m_OutPackets.push( m_Protocol->SEND_SID_ENTERCHAT( ) );
}

void CBNET :: QueueChatCommand( string chatCommand )
{
	if( chatCommand.empty( ) )
		return;

	if( m_LoggedIn )
	{
		if( m_PasswordHashType == "pvpgn" && chatCommand.size( ) > m_MaxMessageLength )
			chatCommand = chatCommand.substr( 0, m_MaxMessageLength );

		if( chatCommand.size( ) > 255 )
			chatCommand = chatCommand.substr( 0, 255 );

		if( m_OutPackets.size( ) > 10 )
			CONSOLE_Print( "[BNET] attempted to queue chat command [" + chatCommand + "] but there are too many (" + UTIL_ToString( m_OutPackets.size( ) ) + ") packets queued, discarding" );
		else
		{
			CONSOLE_Print( "[QUEUED] " + chatCommand );
			m_OutPackets.push( m_Protocol->SEND_SID_CHATCOMMAND( chatCommand ) );
		}
	}
}

void CBNET :: QueueChatCommand( string chatCommand, string user, bool whisper )
{
	if( chatCommand.empty( ) )
		return;

	// if whisper is true send the chat command as a whisper to user, otherwise just queue the chat command

	if( whisper )
		QueueChatCommand( "/w " + user + " " + chatCommand );
	else
		QueueChatCommand( chatCommand );
}

void CBNET :: QueueGetGameList( uint32_t numGames )
{
	if( m_LoggedIn )
		m_OutPackets.push( m_Protocol->SEND_SID_GETADVLISTEX( string( ), numGames ) );
}

void CBNET :: QueueGetGameList( string gameName )
{
	if( m_LoggedIn )
		m_OutPackets.push( m_Protocol->SEND_SID_GETADVLISTEX( gameName, 1 ) );
}

void CBNET :: QueueJoinGame( string gameName )
{
	if( m_LoggedIn )
	{
		m_OutPackets.push( m_Protocol->SEND_SID_NOTIFYJOIN( gameName ) );
		m_InChat = false;
		m_InGame = true;
	}
}
