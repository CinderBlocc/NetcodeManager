#pragma once
#include "bakkesmod/plugin/bakkesmodplugin.h"

/*
* NetcodeManager should be included in plugins that need to have two-way communication in LAN matches.
* When a client sends a custom message to the host, that message will be automatically replicated to all the other clients.
* 
* When including NetcodeManager in a plugin, store it as a std::shared_ptr so that you can construct it in the onLoad function.
*/

class NetcodeManager
{
public:
    //CONSTRUCTOR
    // "InCvarManager" and "InGameWrapper" store the plugin's auto-included "cvarManager" and "gameWrapper" pointers
    //
    // "InPluginExports" stores the parent plugin's auto-included "exports" variable
    //
    // "InMessageHandlingFunction" stores a pointer to the plugin's message handling function
    //     Example function: "void ParentPlugin::ReceiveMessage(const std::string& Message, PriWrapper Sender);"
    //     Pass into constructor as: "std::bind(&ParentPlugin::ReceiveMessage, this, _1, _2)" where _1 and _2 are std::placeholders
    //
    // Example constructor inside onLoad function:
    //     MyNetcodeManager = std::make_shared<NetcodeManager>(cvarManager, gameWrapper, exports, std::bind(&NetcodePluginExample::OnMessageReceived, this, _1, _2));
    NetcodeManager
    (
        std::shared_ptr<CVarManagerWrapper> InCvarManager,
        std::shared_ptr<GameWrapper> InGameWrapper,
        BakkesMod::Plugin::PluginInfo InPluginExports,
        std::function<void (const std::string& Message, PriWrapper Sender)> InMessageHandlingFunction
    );

    //MESSAGE SENDING
    // NOTE: Message cannot be longer than 122 characters minus the length of the plugin's class name.
    // Messages are limited to 128 characters total, and all messages have the following prefix: [PC][ClassName]
    // PC (or PH) is internal info for NetcodePlugin's replication, and ClassName indicates that clients of this plugin should handle the message.
    void SendNewMessage(const std::string& InMessage);



//// NetcodeManager internals ////
private:
    //Cvars and macros from NetcodePlugin
    #define CVAR_MESSAGE_OUT "NETCODE_Message_Out"
    #define CVAR_MESSAGE_IN "NETCODE_Message_In"
    #define CVAR_LOG_LEVEL "NETCODE_Log_Level"

    std::shared_ptr<int> CvarLogLevel;

    #define NETLOGA(x) if(*CvarLogLevel > 0) { cvarManager->log("(A: " + std::to_string(clock()) + ")   " + x); }
    #define NETLOGB(x) if(*CvarLogLevel > 1) { cvarManager->log("(B: " + std::to_string(clock()) + ")   " + x); }
    #define NETLOGC(x) if(*CvarLogLevel > 2) { cvarManager->log("(C: " + std::to_string(clock()) + ")   " + x); }

    //Information about parent plugin
    std::shared_ptr<CVarManagerWrapper> cvarManager;
    std::shared_ptr<GameWrapper> gameWrapper;
    BakkesMod::Plugin::PluginInfo PluginExports;
    std::function<void (const std::string&, PriWrapper)> MessageHandlingFunction;

    //Loading checks
    void NetcodeLoadLoop(bool bResetAttempts);
    bool IsNetcodeLoaded();
    bool DoesNetcodePluginExist();
    void OnSuccessfulLoadDetection();

    //General functionality
    bool bIsGood = false;
    bool CheckIfGood(const char* InFunctionName);

    //Determining authority
    enum class EAuthority
    {
        None = 0,
        Client,
        Host
    };
    EAuthority GetMatchAuthority();
    ServerWrapper GetCurrentGameState();

    //Message receiving and parsing
    struct ParsedMessageData
    {
        std::string PluginClassName;
        PriWrapper  Sender = PriWrapper((uintptr_t)nullptr);
        std::string MessageContent;
    };
    void ReceiveMessage();
    ParsedMessageData ParseIncomingMessage(const std::string& IncomingMessage);
    std::string GetContentFromBrackets(const std::string& IncomingMessage, const size_t InStartPoint, size_t& OutEndPoint);
    PriWrapper GetSenderPri(const std::string& InPriAddressString);

    //Additional logging
    void LogMessageData(ParsedMessageData InMessageData);
    
    //No default constructor
    NetcodeManager() = delete;
};
