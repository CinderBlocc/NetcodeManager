#include "NetcodeManager.h"
#include "bakkesmod/wrappers/PluginManagerWrapper.h"
#include <sstream>
#include <filesystem>

// Constructor //
NetcodeManager::NetcodeManager
(
    std::shared_ptr<CVarManagerWrapper> InCvarManager,
    std::shared_ptr<GameWrapper> InGameWrapper,
    BakkesMod::Plugin::PluginInfo InPluginExports,
    std::function<void (const std::string& Message, PriWrapper Sender)> InMessageHandlingFunction
) : cvarManager(InCvarManager), gameWrapper(InGameWrapper), PluginExports(InPluginExports), MessageHandlingFunction(InMessageHandlingFunction)
{
    bIsGood = false;
    NetcodeLoadLoop(true);
}

// Message Sending //
void NetcodeManager::SendNewMessage(const std::string& InMessage)
{
    if(!CheckIfGood(__FUNCTION__)) { return; }
    
    std::string OutMessage = std::string("[") + PluginExports.className + "]" + InMessage;

    NETLOGC("Sending NetcodeManager message: " + OutMessage);

    //Notify NetcodePlugin that this client wants to send a message
    cvarManager->getCvar(CVAR_MESSAGE_OUT).setValue(OutMessage);
}


// NetcodePlugin Loading //
void NetcodeManager::NetcodeLoadLoop(bool bResetAttempts)
{
    //Check if NetcodeManager is already good to go
    if(CheckIfGood(__FUNCTION__)) { return; }

    //Limit the number of times this loop fires to 20 attempts
    static int LoadAttempts = 0;
    if(bResetAttempts) { LoadAttempts = 0; }
    ++LoadAttempts;
    if(LoadAttempts >= 20) { return; }

    //Check if NetcodePlugin is loaded. Handle the result
    if(IsNetcodeLoaded())
    {
        OnSuccessfulLoadDetection();
    }
    else
    {
        //NetcodePlugin is not loaded. Check if NetcodePlugin.dll exists
        if(DoesNetcodePluginExist())
        {
            cvarManager->executeCommand("plugin load NetcodePlugin", false);
        }
        else
        {
            cvarManager->executeCommand("bpm_install 166", false);
        }

        //Check again to see if the plugin is loaded in 2 seconds
        gameWrapper->SetTimeout(std::bind(&NetcodeManager::NetcodeLoadLoop, this, false), 2.f);
    }
}

bool NetcodeManager::DoesNetcodePluginExist()
{
    return std::filesystem::exists(gameWrapper->GetBakkesModPath() / "plugins" / "NetcodePlugin.dll");
}

bool NetcodeManager::IsNetcodeLoaded()
{
    PluginManagerWrapper PluginManager = gameWrapper->GetPluginManager();
    if(PluginManager.memory_address == NULL) { return false; }

    auto* PluginList = PluginManager.GetLoadedPlugins();
    for(const auto& ThisPlugin : *PluginList)
    {
        if(std::string(ThisPlugin->_details->className) == "NetcodePlugin")
        {
            return true;
        }
    }

    return false;
}

void NetcodeManager::OnSuccessfulLoadDetection()
{
    //Get log level cvar from NetcodePlugin
    CVarWrapper LogLevelCvar = cvarManager->getCvar(CVAR_LOG_LEVEL);
    if(LogLevelCvar.IsNull())
    {
        //Can't use NETLOGA here because it requires CvarLogLevel to be set up
        cvarManager->log("NetcodePlugin is loaded, but could not find cvar " + LogLevelCvar.getCVarName());
        return;
    }

    //Set up log level
    CvarLogLevel = std::make_shared<int>(1);
    LogLevelCvar.bindTo(CvarLogLevel);

    //Check if NetcodePlugin cvars are correct
    CVarWrapper IncomingMessageCvar = cvarManager->getCvar(CVAR_MESSAGE_IN);
    CVarWrapper OutgoingMessageCvar = cvarManager->getCvar(CVAR_MESSAGE_OUT);
    if(IncomingMessageCvar.IsNull())
    {
        NETLOGA("NetcodePlugin is loaded, but could not find cvar " + IncomingMessageCvar.getCVarName());
        return;
    }
    if(OutgoingMessageCvar.IsNull())
    {
        NETLOGA("NetcodePlugin is loaded, but could not find cvar " + OutgoingMessageCvar.getCVarName());
        return;
    }

    //Subscribe to incoming message cvar to see when it has changed, indicating an incoming messaage
    IncomingMessageCvar.addOnValueChanged(std::bind(&NetcodeManager::ReceiveMessage, this));

    //NetcodeManager is all set and can fully function
    bIsGood = true;

    NETLOGA("NetcodeManager has successfully detected that NetcodePlugin is loaded. Ready to go.");
}


// General Functionality //
bool NetcodeManager::CheckIfGood(const char* InFunctionName)
{
    if(!bIsGood)
    {
        cvarManager->log(std::string("NetcodeManager function (") + InFunctionName + ") failed. NetcodePlugin is not loaded.");
        return false;
    }

    return true;
}


// Determining Authority //
ServerWrapper NetcodeManager::GetCurrentGameState()
{
    if(gameWrapper->IsInReplay())
        return gameWrapper->GetGameEventAsReplay().memory_address;
    else if(gameWrapper->IsInOnlineGame())
        return gameWrapper->GetOnlineGame();
    else
        return gameWrapper->GetGameEventAsServer();
}

NetcodeManager::EAuthority NetcodeManager::GetMatchAuthority()
{
    ServerWrapper Server = GetCurrentGameState();
    if(!Server.IsNull())
    {
        GameSettingPlaylistWrapper GSPW = Server.GetPlaylist();
        if(GSPW.memory_address != NULL && GSPW.IsLanMatch())
        {
            if(gameWrapper->IsInOnlineGame())
            {
                return EAuthority::Client;
            }

            return EAuthority::Host;
        }
    }

    return EAuthority::None;
}


// Message Receiving and Parsing //
void NetcodeManager::ReceiveMessage()
{
    if(!CheckIfGood(__FUNCTION__)) { return; }

    std::string IncomingMessage = cvarManager->getCvar(CVAR_MESSAGE_IN).getStringValue();
    NETLOGC("Receiving message: " + IncomingMessage);

    //Get all data from the message
    ParsedMessageData MessageData = ParseIncomingMessage(IncomingMessage);
    
    //Only handle messages that correspond with this plugin
    if(MessageData.PluginClassName != PluginExports.className)
    {
        return;
    }

    LogMessageData(MessageData);

    //Notify the plugin that this message is intended for this plugin
    MessageHandlingFunction(MessageData.MessageContent, MessageData.Sender);
}

NetcodeManager::ParsedMessageData NetcodeManager::ParseIncomingMessage(const std::string& IncomingMessage)
{
    //Incoming message format should be as follows:
    //[PluginName][SenderPriAddress]Message

    //Split message based on matched brackets [] and fill data struct
    size_t StartPoint = 0, EndPoint = 0;
    ParsedMessageData Output;

    //Get class name
    Output.PluginClassName = GetContentFromBrackets(IncomingMessage, StartPoint, EndPoint);
    StartPoint = EndPoint + 1;

    //Get sender pri
    Output.Sender = GetSenderPri(GetContentFromBrackets(IncomingMessage, StartPoint, EndPoint));
    StartPoint = EndPoint + 1;

    //Get message content
    Output.MessageContent = IncomingMessage.substr(StartPoint, INT_MAX);

    return Output;
}

std::string NetcodeManager::GetContentFromBrackets(const std::string& IncomingMessage, const size_t InStartPoint, size_t& OutEndPoint)
{
    std::string Output;

    if(!IncomingMessage.empty() && InStartPoint < IncomingMessage.size())
    {
        if(IncomingMessage.at(InStartPoint) == '[')
        {
            OutEndPoint = IncomingMessage.find_first_of(']', InStartPoint);
            if(OutEndPoint != std::string::npos)
            {
                Output = IncomingMessage.substr(InStartPoint + 1, OutEndPoint - InStartPoint - 1);
            }
        }
    }

    return Output;
}

PriWrapper NetcodeManager::GetSenderPri(const std::string& InChatterPriAddressString)
{
    uintptr_t PriAddress = NULL;

    if(!InChatterPriAddressString.empty() && InChatterPriAddressString != "0")
    {
        std::stringstream ChatterAddressStream(InChatterPriAddressString);
        ChatterAddressStream >> PriAddress;
    }

    return PriWrapper(PriAddress);
}


// Additional logging //
void NetcodeManager::LogMessageData(ParsedMessageData InMessageData)
{
    std::string Output = "Parsed message:\n";

    //Plugin name
    Output += "PluginClassName: " + InMessageData.PluginClassName + '\n';

    //Original chatter
    Output += "Sender: ";
    std::string OriginalChatterName = "NULL";
    if(!InMessageData.Sender.IsNull())
    {
        if(!InMessageData.Sender.GetPlayerName().IsNull())
        {
            OriginalChatterName = InMessageData.Sender.GetPlayerName().ToString();
        }
    }
    Output += OriginalChatterName + '\n';

    //Message content
    Output += "MessageContent: " + InMessageData.MessageContent;

    NETLOGC(Output);
}
