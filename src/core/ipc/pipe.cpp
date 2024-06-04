#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "pipe.hpp"
#include <core/ffi/ffi.hpp>
#include <utilities/encoding.h>
#include <core/ipc/pipe.hpp>
#include <functional>

typedef websocketpp::server<websocketpp::config::asio> server;

static nlohmann::json CallServerMethod(nlohmann::basic_json<> message)
{
    const std::string fnCallScript = Python::ConstructFunctionCall(message["data"]);

    if (!message["data"].contains("pluginName")) 
    {
        Logger.Error("no plugin backend specified, doing nothing...");
        return {};
    }

    Python::EvalResult response = Python::LockGILAndEvaluate(message["data"]["pluginName"], fnCallScript);
    nlohmann::json responseMessage;

    switch (response.type)
    {
        case Python::ReturnTypes::Boolean:   { responseMessage["returnValue"] = (response.plain == "True" ? true : false); break; }
        case Python::ReturnTypes::String:    { responseMessage["returnValue"] = Base64Encode(response.plain); break; }
        case Python::ReturnTypes::Integer:   { responseMessage["returnValue"] = stoi(response.plain); break; }

        case Python::ReturnTypes::Error: 
        {
            responseMessage["failedRequest"] = true;
            responseMessage["failMessage"] = response.plain;
            break;
        }
    }

    responseMessage["id"] = message["iteration"];
    return responseMessage; 
}

static nlohmann::json OnFrontEndLoaded(nlohmann::basic_json<> message)
{
    Logger.Log("Notifying {} that the frontend loaded", message["data"]["pluginName"].get<std::string>());

    Python::LockGILAndDiscardEvaluate(
        message["data"]["pluginName"],
        "plugin._front_end_loaded()"
    );

    return nlohmann::json({
        { "id", message["iteration"] },
        { "success", true }
    });
}

void OnMessage(server* serv, websocketpp::connection_hdl hdl, server::message_ptr msg) {

    server::connection_ptr con = serv->get_con_from_hdl(hdl);
    try
    {
        auto json_data = nlohmann::json::parse(msg->get_payload());
        std::string responseMessage;

        switch (json_data["id"].get<int>()) 
        {
            case IPCMain::Builtins::CALL_SERVER_METHOD: 
            {
                responseMessage = CallServerMethod(json_data).dump(); 
                break;
            }
            case IPCMain::Builtins::FRONT_END_LOADED: 
            {
                responseMessage = OnFrontEndLoaded(json_data).dump(); 
                break;
            }
        }
        con->send(responseMessage, msg->get_opcode());
    }
    catch (nlohmann::detail::exception& ex) 
    {
        con->send(std::string(ex.what()), msg->get_opcode());
    }
    catch (std::exception& ex) 
    {
        con->send(std::string(ex.what()), msg->get_opcode());
    }
}

void OnOpen(server* IPCSocketMain, websocketpp::connection_hdl hdl) 
{
    IPCSocketMain->start_accept();
}

const int OpenIPCSocket() 
{
    server IPCSocketMain;

    try {
        IPCSocketMain.set_access_channels(websocketpp::log::alevel::none);
        IPCSocketMain.clear_access_channels(websocketpp::log::alevel::all);

        IPCSocketMain.init_asio();
        IPCSocketMain.set_message_handler(bind(OnMessage, &IPCSocketMain, std::placeholders::_1, std::placeholders::_2));
        IPCSocketMain.set_open_handler(bind(&OnOpen, &IPCSocketMain, std::placeholders::_1));

        IPCSocketMain.listen(12906);
        IPCSocketMain.start_accept();
        IPCSocketMain.run();
    }
    catch (const std::exception& e) {
        Logger.Error("[pipecon] uncaught error -> {}", e.what());
        return false;
    }
    return true;
}

const int IPCMain::OpenConnection()
{
    std::thread(OpenIPCSocket).detach();
    return true;
}
