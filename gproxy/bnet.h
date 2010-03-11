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

#ifndef BNET_H
#define BNET_H

//
// CBNET
//

class CTCPClient;
class CCommandPacket;
class CBNCSUtilInterface;
class CBNETProtocol;
// class CBNLSClient;
class CIncomingFriendList;
class CIncomingClanList;
class CIncomingChatEvent;

class CBNET
{
public:
	CGProxy *m_GProxy;

private:
	CTCPClient *m_Socket;							// the connection to battle.net
	CBNETProtocol *m_Protocol;						// battle.net protocol
//	CBNLSClient *m_BNLSClient;						// the BNLS client (for external warden handling)
	queue<CCommandPacket *> m_Packets;				// queue of incoming packets
	CBNCSUtilInterface *m_BNCSUtil;					// the interface to the bncsutil library (used for logging into battle.net)
	queue<BYTEARRAY> m_OutPackets;					// queue of outgoing packets to be sent (to prevent getting kicked for flooding)
	string m_Server;								// battle.net server to connect to
	string m_ServerIP;								// battle.net server to connect to (the IP address so we don't have to resolve it every time we connect)
	string m_BNLSServer;							// BNLS server to connect to (for warden handling)
	uint16_t m_BNLSPort;							// BNLS port
	uint32_t m_BNLSWardenCookie;					// BNLS warden cookie
	string m_CDKeyROC;								// ROC CD key
	string m_CDKeyTFT;								// TFT CD key
	string m_CountryAbbrev;							// country abbreviation
	string m_Country;								// country
	string m_UserName;								// battle.net username
	string m_UserPassword;							// battle.net password
	string m_FirstChannel;							// the first chat channel to join upon entering chat (note: we hijack this to store the last channel when entering a game)
	string m_CurrentChannel;						// the current chat channel
	bool m_ListPublicGames;
	string m_PublicGameFilter;
	string m_SearchGameName;
	uint32_t m_SearchGameNameTime;
	string m_ReplyTarget;
	unsigned char m_War3Version;					// custom warcraft 3 version for PvPGN users
	BYTEARRAY m_EXEVersion;							// custom exe version for PvPGN users
	BYTEARRAY m_EXEVersionHash;						// custom exe version hash for PvPGN users
	string m_PasswordHashType;						// password hash type for PvPGN users
	uint32_t m_MaxMessageLength;					// maximum message length for PvPGN users
	uint32_t m_LastDisconnectedTime;				// GetTime when we were last disconnected from battle.net
	uint32_t m_LastConnectionAttemptTime;			// GetTime when we last attempted to connect to battle.net
	uint32_t m_LastNullTime;						// GetTime when the last null packet was sent for detecting disconnects
	uint32_t m_LastOutPacketTicks;					// GetTicks when the last packet was sent for the m_OutPackets queue
	uint32_t m_LastOutPacketSize;
	uint32_t m_LastGetPublicListTime;
	uint32_t m_LastGetSearchGameTime;
	bool m_FirstConnect;							// if we haven't tried to connect to battle.net yet
	bool m_WaitingToConnect;						// if we're waiting to reconnect to battle.net after being disconnected
	bool m_LoggedIn;								// if we've logged into battle.net or not
	bool m_InChat;									// if we've entered chat or not (but we're not necessarily in a chat channel yet)
	bool m_InGame;

public:
	CBNET( CGProxy *nGProxy, string nServer, string nBNLSServer, uint16_t nBNLSPort, uint32_t nBNLSWardenCookie, string nCDKeyROC, string nCDKeyTFT, string nCountryAbbrev, string nCountry, string nUserName, string nUserPassword, string nFirstChannel, unsigned char nWar3Version, BYTEARRAY nEXEVersion, BYTEARRAY nEXEVersionHash, string nPasswordHashType, uint32_t nMaxMessageLength );
	~CBNET( );

	string GetServer( )					{ return m_Server; }
	string GetCDKeyROC( )				{ return m_CDKeyROC; }
	string GetCDKeyTFT( )				{ return m_CDKeyTFT; }
	string GetUserName( )				{ return m_UserName; }
	string GetUserPassword( )			{ return m_UserPassword; }
	string GetFirstChannel( )			{ return m_FirstChannel; }
	string GetCurrentChannel( )			{ return m_CurrentChannel; }
	bool GetListPublicGames( )			{ return m_ListPublicGames; }
	string GetPublicGameFilter( )		{ return m_PublicGameFilter; }
	string GetSearchGameName( )			{ return m_SearchGameName; }
	string GetReplyTarget( )			{ return m_ReplyTarget; }
	BYTEARRAY GetEXEVersion( )			{ return m_EXEVersion; }
	BYTEARRAY GetEXEVersionHash( )		{ return m_EXEVersionHash; }
	string GetPasswordHashType( )		{ return m_PasswordHashType; }
	bool GetLoggedIn( )					{ return m_LoggedIn; }
	bool GetInChat( )					{ return m_InChat; }
	bool GetInGame( )					{ return m_InGame; }
	uint32_t GetOutPacketsQueued( )		{ return m_OutPackets.size( ); }
	BYTEARRAY GetUniqueName( );

	void SetListPublicGames( bool nListPublicGames )		{ m_ListPublicGames = nListPublicGames; }
	void SetPublicGameFilter( string nPublicGameFilter )	{ m_PublicGameFilter = nPublicGameFilter; }
	void SetSearchGameName( string nSearchGameName )		{ m_SearchGameName = nSearchGameName; m_SearchGameNameTime = GetTime( ); }

	// processing functions

	unsigned int SetFD( void *fd, void *send_fd, int *nfds );
	bool Update( void *fd, void *send_fd );
	void ExtractPackets( );
	void ProcessPackets( );
	void ProcessChatEvent( CIncomingChatEvent *chatEvent );

	// functions to send packets to battle.net

	void SendJoinChannel( string channel );
	void QueueEnterChat( );
	void QueueChatCommand( string chatCommand );
	void QueueChatCommand( string chatCommand, string user, bool whisper );
	void QueueGetGameList( uint32_t numGames );
	void QueueGetGameList( string gameName );
	void QueueJoinGame( string gameName );
};

#endif
