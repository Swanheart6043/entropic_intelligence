#pragma once
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <memory>
#include <string>

class DBConnection {
public:
    static DBConnection& get_instance();
    sql::Connection* get_connection();

private:
    DBConnection() = default;
    std::unique_ptr<sql::Connection> create_connection();
};