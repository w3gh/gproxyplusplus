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
#include "gameprotocol.h"

//
// CGameProtocol
//

CGameProtocol :: CGameProtocol( CGProxy *nGProxy )
{
	m_GProxy = nGProxy;
}

CGameProtocol :: ~CGameProtocol( )
{

}

///////////////////////
// RECEIVE FUNCTIONS //
///////////////////////

////////////////////
// SEND FUNCTIONS //
////////////////////

BYTEARRAY CGameProtocol :: SEND_W3GS_CHAT_FROM_HOST( unsigned char fromPID, BYTEARRAY toPIDs, unsigned char flag, BYTEARRAY flagExtra, string message )
{
	BYTEARRAY packet;

	if( !toPIDs.empty( ) && !message.empty( ) && message.size( ) < 255 )
	{
		packet.push_back( W3GS_HEADER_CONSTANT );		// W3GS header constant
		packet.push_back( W3GS_CHAT_FROM_HOST );		// W3GS_CHAT_FROM_HOST
		packet.push_back( 0 );							// packet length will be assigned later
		packet.push_back( 0 );							// packet length will be assigned later
		packet.push_back( toPIDs.size( ) );				// number of receivers
		UTIL_AppendByteArrayFast( packet, toPIDs );		// receivers
		packet.push_back( fromPID );					// sender
		packet.push_back( flag );						// flag
		UTIL_AppendByteArrayFast( packet, flagExtra );	// extra flag
		UTIL_AppendByteArrayFast( packet, message );	// message
		AssignLength( packet );
	}
	else
		CONSOLE_Print( "[GAMEPROTO] invalid parameters passed to SEND_W3GS_CHAT_FROM_HOST" );

	// DEBUG_Print( "SENT W3GS_CHAT_FROM_HOST" );
	// DEBUG_Print( packet );
	return packet;
}

BYTEARRAY CGameProtocol :: SEND_W3GS_SEARCHGAME( bool TFT, unsigned char war3Version )
{
	unsigned char ProductID_ROC[]	= {          51, 82, 65, 87 };	// "WAR3"
	unsigned char ProductID_TFT[]	= {          80, 88, 51, 87 };	// "W3XP"
	unsigned char Version[]			= { war3Version,  0,  0,  0 };
	unsigned char Unknown[]			= {           0,  0,  0,  0 };

	BYTEARRAY packet;
	packet.push_back( W3GS_HEADER_CONSTANT );				// W3GS header constant
	packet.push_back( W3GS_SEARCHGAME );					// W3GS_SEARCHGAME
	packet.push_back( 0 );									// packet length will be assigned later
	packet.push_back( 0 );									// packet length will be assigned later

	if( TFT )
		UTIL_AppendByteArray( packet, ProductID_TFT, 4 );	// Product ID (TFT)
	else
		UTIL_AppendByteArray( packet, ProductID_ROC, 4 );	// Product ID (ROC)

	UTIL_AppendByteArray( packet, Version, 4 );				// Version
	UTIL_AppendByteArray( packet, Unknown, 4 );				// ???
	AssignLength( packet );
	// DEBUG_Print( "SENT W3GS_SEARCHGAME" );
	// DEBUG_Print( packet );
	return packet;
}

BYTEARRAY CGameProtocol :: SEND_W3GS_GAMEINFO( bool TFT, unsigned char war3Version, BYTEARRAY mapGameType, BYTEARRAY mapFlags, BYTEARRAY mapWidth, BYTEARRAY mapHeight, string gameName, string hostName, uint32_t upTime, string mapPath, BYTEARRAY mapCRC, uint32_t slotsTotal, uint32_t slotsOpen, uint16_t port, uint32_t hostCounter, uint32_t entryKey )
{
	unsigned char ProductID_ROC[]	= {          51, 82, 65, 87 };	// "WAR3"
	unsigned char ProductID_TFT[]	= {          80, 88, 51, 87 };	// "W3XP"
	unsigned char Version[]			= { war3Version,  0,  0,  0 };
	unsigned char Unknown[]			= {           1,  0,  0,  0 };

	BYTEARRAY packet;

	if( mapGameType.size( ) == 4 && mapFlags.size( ) == 4 && mapWidth.size( ) == 2 && mapHeight.size( ) == 2 && !gameName.empty( ) && !hostName.empty( ) && !mapPath.empty( ) && mapCRC.size( ) == 4 )
	{
		// make the stat string

		BYTEARRAY StatString;
		UTIL_AppendByteArrayFast( StatString, mapFlags );
		StatString.push_back( 0 );
		UTIL_AppendByteArrayFast( StatString, mapWidth );
		UTIL_AppendByteArrayFast( StatString, mapHeight );
		UTIL_AppendByteArrayFast( StatString, mapCRC );
		UTIL_AppendByteArrayFast( StatString, mapPath );
		UTIL_AppendByteArrayFast( StatString, hostName );
		StatString.push_back( 0 );
		StatString = UTIL_EncodeStatString( StatString );

		// make the rest of the packet

		packet.push_back( W3GS_HEADER_CONSTANT );						// W3GS header constant
		packet.push_back( W3GS_GAMEINFO );								// W3GS_GAMEINFO
		packet.push_back( 0 );											// packet length will be assigned later
		packet.push_back( 0 );											// packet length will be assigned later

		if( TFT )
			UTIL_AppendByteArray( packet, ProductID_TFT, 4 );			// Product ID (TFT)
		else
			UTIL_AppendByteArray( packet, ProductID_ROC, 4 );			// Product ID (ROC)

		UTIL_AppendByteArray( packet, Version, 4 );						// Version
		UTIL_AppendByteArray( packet, hostCounter, false );				// Host Counter
		UTIL_AppendByteArray( packet, entryKey, false );				// Entry Key
		UTIL_AppendByteArrayFast( packet, gameName );					// Game Name
		packet.push_back( 0 );											// ??? (maybe game password)
		UTIL_AppendByteArrayFast( packet, StatString );					// Stat String
		packet.push_back( 0 );											// Stat String null terminator (the stat string is encoded to remove all even numbers i.e. zeros)
		UTIL_AppendByteArray( packet, slotsTotal, false );				// Slots Total
		UTIL_AppendByteArrayFast( packet, mapGameType );				// Game Type
		UTIL_AppendByteArray( packet, Unknown, 4 );						// ???
		UTIL_AppendByteArray( packet, slotsOpen, false );				// Slots Open
		UTIL_AppendByteArray( packet, upTime, false );					// time since creation
		UTIL_AppendByteArray( packet, port, false );					// port
		AssignLength( packet );
	}
	else
		CONSOLE_Print( "[GAMEPROTO] invalid parameters passed to SEND_W3GS_GAMEINFO" );

	// DEBUG_Print( "SENT W3GS_GAMEINFO" );
	// DEBUG_Print( packet );
	return packet;
}

BYTEARRAY CGameProtocol :: SEND_W3GS_CREATEGAME( bool TFT, unsigned char war3Version )
{
	unsigned char ProductID_ROC[]	= {          51, 82, 65, 87 };	// "WAR3"
	unsigned char ProductID_TFT[]	= {          80, 88, 51, 87 };	// "W3XP"
	unsigned char Version[]			= { war3Version,  0,  0,  0 };
	unsigned char HostCounter[]		= {           1,  0,  0,  0 };

	BYTEARRAY packet;
	packet.push_back( W3GS_HEADER_CONSTANT );				// W3GS header constant
	packet.push_back( W3GS_CREATEGAME );					// W3GS_CREATEGAME
	packet.push_back( 0 );									// packet length will be assigned later
	packet.push_back( 0 );									// packet length will be assigned later

	if( TFT )
		UTIL_AppendByteArray( packet, ProductID_TFT, 4 );	// Product ID (TFT)
	else
		UTIL_AppendByteArray( packet, ProductID_ROC, 4 );	// Product ID (ROC)

	UTIL_AppendByteArray( packet, Version, 4 );				// Version
	UTIL_AppendByteArray( packet, HostCounter, 4 );			// Host Counter
	AssignLength( packet );
	// DEBUG_Print( "SENT W3GS_CREATEGAME" );
	// DEBUG_Print( packet );
	return packet;
}

BYTEARRAY CGameProtocol :: SEND_W3GS_REFRESHGAME( uint32_t players, uint32_t playerSlots )
{
	unsigned char HostCounter[]	= { 1, 0, 0, 0 };

	BYTEARRAY packet;
	packet.push_back( W3GS_HEADER_CONSTANT );			// W3GS header constant
	packet.push_back( W3GS_REFRESHGAME );				// W3GS_REFRESHGAME
	packet.push_back( 0 );								// packet length will be assigned later
	packet.push_back( 0 );								// packet length will be assigned later
	UTIL_AppendByteArray( packet, HostCounter, 4 );		// Host Counter
	UTIL_AppendByteArray( packet, players, false );		// Players
	UTIL_AppendByteArray( packet, playerSlots, false );	// Player Slots
	AssignLength( packet );
	// DEBUG_Print( "SENT W3GS_REFRESHGAME" );
	// DEBUG_Print( packet );
	return packet;
}

BYTEARRAY CGameProtocol :: SEND_W3GS_DECREATEGAME( uint32_t hostCounter )
{
	BYTEARRAY packet;
	packet.push_back( W3GS_HEADER_CONSTANT );			// W3GS header constant
	packet.push_back( W3GS_DECREATEGAME );				// W3GS_DECREATEGAME
	packet.push_back( 0 );								// packet length will be assigned later
	packet.push_back( 0 );								// packet length will be assigned later
	UTIL_AppendByteArray( packet, hostCounter, false );	// Host Counter
	AssignLength( packet );
	// DEBUG_Print( "SENT W3GS_DECREATEGAME" );
	// DEBUG_Print( packet );
	return packet;
}

/////////////////////
// OTHER FUNCTIONS //
/////////////////////

bool CGameProtocol :: AssignLength( BYTEARRAY &content )
{
	// insert the actual length of the content array into bytes 3 and 4 (indices 2 and 3)

	BYTEARRAY LengthBytes;

	if( content.size( ) >= 4 && content.size( ) <= 65535 )
	{
		LengthBytes = UTIL_CreateByteArray( (uint16_t)content.size( ), false );
		content[2] = LengthBytes[0];
		content[3] = LengthBytes[1];
		return true;
	}

	return false;
}

bool CGameProtocol :: ValidateLength( BYTEARRAY &content )
{
	// verify that bytes 3 and 4 (indices 2 and 3) of the content array describe the length

	uint16_t Length;
	BYTEARRAY LengthBytes;

	if( content.size( ) >= 4 && content.size( ) <= 65535 )
	{
		LengthBytes.push_back( content[2] );
		LengthBytes.push_back( content[3] );
		Length = UTIL_ByteArrayToUInt16( LengthBytes, false );

		if( Length == content.size( ) )
			return true;
	}

	return false;
}
