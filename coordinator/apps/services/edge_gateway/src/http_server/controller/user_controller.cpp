#include <memory>
#include <spdlog/spdlog.h>
#include "user_controller.hpp"
#include "config_parser.hpp"
#include "db_connection.hpp"

std::string UserController::login(const httplib::Request& req) {
    using namespace nlohmann;

    json body = json::parse(req.body);          
    std::string username = body.at("username");
    std::string password = body.at("password");

    try {
        sql::Connection* con = DBConnection::get_instance().get_connection();

        std::unique_ptr<sql::PreparedStatement> stmt(con->prepareStatement(
            "SELECT username FROM android_server_user WHERE username = ? AND password = ?"
        ));
        stmt->setString(1, username);
        stmt->setString(2, password);

        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

        if (!res->next()) return fail(500, i18n::Msg::IncorrectAccountPassword);

        return success();
    } catch (sql::SQLException& e) {
        SPDLOG_ERROR("数据库错误: {}", e.what());
        SPDLOG_ERROR("错误代码: {}", e.getErrorCode());
        SPDLOG_ERROR("SQL状态: {}", e.getSQLState());
        return fail(500, i18n::Msg::InternalError);
    }
}