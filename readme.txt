=================================================
GProxy++ Public Test Release 1.0 (March 11, 2010)
=================================================

GProxy++ is a "disconnect protection" tool for use with Warcraft III when connecting to specially configured GHost++ servers.
It allows the game to recover from a temporary internet disruption which would normally cause a disconnect.
The game will typically pause and display the lag screen while waiting for a player to reconnect although this is configurable on the server side.
There is a limit to how long the game will wait for a player to reconnect although this is also configurable on the server side.
Players can still vote to drop a player while waiting for them to reconnect and if enough players vote they will be dropped and the game will resume.

You can use GProxy++ to connect to ANY games, GHost++ hosted or otherwise.
Players can also connect to reconnect enabled servers even without using GProxy++, they just won't be protected if they get a disconnect.
You can also use GProxy++ for regular chat or channel monitoring.
There are a few extra features that help you filter game lists based on the type of game you are looking for, make note of the commands below.
Any games created by a reconnect-enabled host will be listed in blue in your game list on the LAN screen.

=========================
Where can I use GProxy++?
=========================

This version of GProxy++ uses the LAN connection to find games through battle.net and PVPGN networks, but it is open source and can be freely adapted to work with any other platforms.
Many are looking into adapting it for their platforms as we speak.

=============
Configuration
=============

When you start GProxy++ for the first time it will ask you to enter some required information.
If you want to use GProxy++ on battle.net the questions it asks you are all you need to answer.
If you want to use GProxy++ on PVPGN you will need to enter some additional information after the program starts.

To use GProxy++ on PVPGN:

When the program asks you which realm you want to connect to, enter the address of the PVPGN server.
After the program starts type /quit to quit.
Open the file "gproxy.cfg" in a text editor and enter some information for exeversion and exeversionhash and set "passwordhashtype = pvpgn".
Your PVPGN server operator will provide you with the information you need to enter for exeversion and exeversionhash.

Full list of configuration keys:

log                     the log file to log console output to
war3path		the full path to your Warcraft III folder
cdkeyroc		your Reign of Chaos CD key
cdkeytft		your The Frozen Throne CD key
server			the battle.net/PVPGN server to connect to
username		your battle.net/PVPGN username
password		your battle.net/PVPGN password
channel			the first channel to join after logging into battle.net/PVPGN
war3version		the Warcraft III version to connect to battle.net/PVPGN with
port			the port GProxy++ should listen for the local player on
exeversion		used with PVPGN servers
exeversionhash		used with PVPGN servers
passwordhashtype	used with PVPGN servers (set to "pvpgn" when connecting to a PVPGN server)

Note that GProxy++ is compatible with Warcraft III: Reign of Chaos.
If you do not want to play The Frozen Throne just remove your TFT CD key from the config file or do not enter it when starting GProxy++ for the first time.

====
Help
====

Type /help at any time for help when using GProxy++.

========
Commands
========

In the GProxy++ console:

/commands		show command list
/exit or /quit		close GProxy++
/filter <f>             start filtering public game names for <f>
/filteroff              stop filtering public game names
/game <gamename>	look for a specific game named <gamename>
/help			show help text
/public			enable listing of public games (also: /publicon, /public on, /list, /liston, /list on)
/publicoff		disable listing of public games (also: /public off, /listoff, /list off)
/r <message>		reply to the last received whisper
/start			start Warcraft III
/version		show version text

In game:

/re <message>		reply to the last received whisper
/sc			whispers "spoofcheck" to the game host (also: /spoof, /spoofcheck, /spoof check)
/status			show status information
/w <user> <message>	whispers <message> to <user>

=================
Technical Details
=================

GProxy++, as you may have guessed, proxies the connection from Warcraft III to the GHost++ server.
This is done because Warcraft III is not able to handle a lost connection but, with some trickery, GProxy++ can.
We assume that the (local) connection from Warcraft III to GProxy++ is stable and we permit the (internet) connection from GProxy++ to GHost++ to be unstable.

Warcraft III <----local----> GProxy++ <----internet----> GHost++

When GProxy++ connects to a GHost++ server it sends a GPS_INIT message which signals that the client is using GProxy++.
The server responds with some required information and marks the client as being "reconnectable".
Since the GPS_INIT message is not recognized by non-GHost++ hosts and we want GProxy++ to be able to connect to either, we need some way of identifying GHost++ hosts.
This is done by setting the map dimensions in the stat string to 1984x1984 as these map dimensions are not valid.
The map dimensions are ignored by Warcraft III so there don't seem to be any side effects to this method.
If GProxy++ connects to a game with map dimensions of 1984x1984 it will send the GPS_INIT message, otherwise it will act as a dumb proxy.

After the connection is established both GProxy++ and GHost++ start counting and buffering all the W3GS messages both sent and received.
This is necessary because a broken connection might be detected at different points by either side.
In order to resume the connection we need to ensure the data stream is synchronized which requires starting from a known-good-point.

If GProxy++ detects a broken connection it starts attempting to reconnect to the GHost++ server every 10 seconds.
When a reconnection occurs GProxy++ sends a GPS_RECONNECT message which includes the last message number that it received from the server.
GHost++ then responds with its own GPS_RECONNECT message which includes the last message number that it received from the client.
Both sides then stream all subsequent messages buffered since the requested message numbers and the game resumes.

As an optimization a GPS_ACK message is sent by both sides periodically which allows the other side to remove old messages from the buffer.

Unfortunately there is a quirk in Warcraft III that complicates the reconnection process.
Warcraft III will disconnect from GProxy++ if it doesn't receive a W3GS action at least every 65 seconds or so.
This puts a hard limit on the time we can take to reconnect to the server if the connection is broken.
I decided that 65 seconds was too short so I needed some way to send additional W3GS actions to extend the reconnect time.
However, W3GS actions (even empty ones) are "desyncable" which means we must ensure that every player receives the same actions in the same order.
We can't just have GProxy++ start sending empty actions to the disconnected player because the other players didn't receive those same actions.
And since the broken connection will be detected at different points, we can't go back in time to send empty actions to the connected players after someone disconnects.
The solution, while inelegant, is to send a defined number of empty actions between every single real action.
GProxy++ holds these empty actions in reserve, only sending them to the client when a subsequent real action is received.
Note that as an optimization, GHost++ only sends these empty actions to non-GProxy++ clients.
The number of empty actions is negotiated in the GPS_INIT message and it is assumed that GProxy++ will generate the correct number of empty actions as required.

If GProxy++ doesn't receive a W3GS action for 60 seconds it will "use up" one of the available empty actions to keep the Warcraft III client interested.
If it doesn't have any more empty actions available it will give up and allow Warcraft III to disconnect.
This is because generating another empty action for the Warcraft III client would result in a desync upon reconnection.
After reconnecting GProxy++ knows how many empty actions it had to use while the connection was broken and only sends the remaining number to the Warcraft III client.

GHost++ itself will only bring up the lag screen for a disconnected GProxy++ client when the client falls behind by bot_synclimit messages.
This means it's important for bot_synclimit to be set to a "reasonable value" when allowing GProxy++ reconnections.
Otherwise the game will not be paused while waiting and when GProxy++ reconnects the game will play in fast forward for the player until catching up.
During the catchup period the player will not be able to control their units.

Note that very occasionally the game temporarily enters a state where empty actions cannot be generated.
If the connection is broken while in this state GProxy++ will be limited to 65 seconds to reconnect regardless of the server's configuration.
In most games this will only happen a couple of times for a fraction of a second each time, but note that the lower the server's bot_latency the less chance of this happening.
