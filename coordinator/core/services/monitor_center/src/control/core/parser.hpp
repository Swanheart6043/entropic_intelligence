#pragma once
#include <memory>
#include <stdexcept>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/spdlog.h>
#include <zmq.hpp>
#include "../common/udp_client.hpp"
#include "version_handler.hpp"

// 电源管理信息
#pragma pack(push, 1)
struct BatStateDef {
    uint16_t STA_OUT_EN :1; // 放电mos使能
    uint16_t STA_CHA_EN :1; // 充电mos使能
    uint16_t STA_OUT :1; // 放电状态
    uint16_t STA_CHA :1; // 充电状态
    uint16_t STA_ADV_CHA :1; // 预充电状态
    uint16_t STA_CHAER_CONECT :1; // 适配器连接
    uint16_t STA_SWITCH :1; // 开关状态
    uint16_t bit7 :1; // 预留
    uint16_t bit8 :1; // 预留
    uint16_t bit9 :1; // 预留
    uint16_t bit10 :1; // 预留
    uint16_t bit11 :1; // 预留
    uint16_t bit12 :1; // 预留
    uint16_t batItem :3; // 指代电池厂家,请见BatItemDef
};
struct BatProtectDef {
    uint16_t PRO_CHA_CUR :1; // 充电过流保护
    uint16_t PRO_OUT_CUR :1; // 放电过流保护
    uint16_t PRO_CHA_TEM :1; // 充电温度保护
    uint16_t PRO_OUT_TEM :1; // 放电温度保护
    uint16_t PRO_SHORT :1; // 短路保护
    uint16_t PRO_VOT_LOW :1; // 欠压保护
    uint16_t PRO_VOT_HIGH :1; // 过压保护
    uint16_t PRO_TEMP_HIGH :1; // 环境高温保护
    uint16_t PRO_TEMP_LOW :1; // 环境低温保护
    uint16_t PRO_OVER_FULL :1; // 充满保护
    uint16_t bit10 :1; // 预留
    uint16_t bit11 :1; // 预留
    uint16_t bit12 :1; // 预留
    uint16_t bit13 :1; // 预留
    uint16_t bit14 :1; // 预留
    uint16_t bit15 :1; // 预留
};
struct BatErrDef {
    uint16_t ERR_CELL_VOT :1; // 电芯过压告警
    uint16_t ERR_CELL_OUT :1; // 电芯过放告警
    uint16_t ERR_AVOT_VOT :1; // 总压过压告警
    uint16_t ERR_AVOT_OUT :1; // 总压过放告警
    uint16_t ERR_CHA_CUR :1; // 充电过流告警
    uint16_t ERR_OUT_CUR :1; // 放电过流告警
    uint16_t ERR_CHA_TEM :1; // 充电过温告警
    uint16_t ERR_OUT_TEM :1; // 放电过温告警
    uint16_t ERR_TEMP_HIG :1; // 环境高温告警
    uint16_t ERR_TEMP_LOW :1; // 环境低温告警
    uint16_t ERR_LOW_SOC :1; // SOC 低告警    
    uint16_t ERR_CHA_MOS :1; // 充电MOS故障
    uint16_t ERR_OUT_MOS :1; // 放电MOS故障
    uint16_t ERR_IC :1; // ic故障
    uint16_t bit14 :1; // 预留
    uint16_t bit15 :1; // 预留
};
struct BatteryInfo {
    uint16_t Ver; //版本号
    uint16_t Time; //修订日期
    uint16_t Vot; //实时电压(10mV)
    int16_t Cur; //实时电流(10mA)
    uint8_t SOC; //电量百分比
    int8_t Temp; //温度(摄氏度)
    uint16_t CapNow; //实时容量(0.1Ah)
    uint16_t CapFull; //满电容量(0.1Ah)
    uint16_t ChgNum; //循环次数
    uint16_t CellVotH; //电芯最高电压(10mV)
    uint16_t CellVotL; //电芯最低电压(10mV)
    int8_t CellTempH; //电芯最高温度
    int8_t CellTempL; //电芯最低温度
    BatStateDef State; //状态标志
    BatProtectDef Protect; //保护信息
    BatErrDef Error; //故障信息
};
#pragma pack(pop)

// 充电状态
#pragma pack(push, 1)
union ChargeManageStatueDef {
    uint8_t value;
    struct {
        uint8_t flagAutoCharge :1; //Bit0:自动充电状态 [0]未充电 [1]充电                            
        uint8_t flagManualCharge :1; //Bit1:手动充电状态 [0]未充电 [1]充电    
        uint8_t flagChargingBatOpen :1; //Bit2:自动充电mos开关 [0]未开 [1]已开        
        uint8_t flagAutoConnect :1; //Bit3:自动充电电极对接状态 [0]未连接 [1]连接
        uint8_t flagManualConnect :1; //Bit4:手动充电电极对接状态 [0]未连接 [1]连接            
        uint8_t flagManaChargeOpen :1; //Bit5:手动充电使能 [0]未开 [1]已开
        uint8_t flagChargePileErr :1; //Bit6:充电桩异常/无输出/接触不良 [0]正常 [1]异常
        uint8_t flagChargeErr :1; //Bit7:充电故障/严重/无法进行充电 [0]正常 [1]异常
    }StaFlag;
};
#pragma pack(pop)

// 温湿度信息
#pragma pack(push, 1)
struct TemperatureAndHumidity {
    int16_t temperature; // 温度 单位度, 此值是实际温度*10，如264是26.4度
    uint16_t humidity; // 湿度 百分比, 此值是实际百分比*10 如350是35%
    uint16_t PM2_5; //ug/M3
    uint16_t PM10; //ug/M3
    uint16_t PM1_0; //ug/M3
};
#pragma pack(pop)

// 系统运行周期数据
#pragma pack(push, 1)
struct LifecycleInfoDef {
    uint32_t totalMilleage; // 生命周期总里程 //m
    uint32_t nowMilleage; // 当次开机总里程 //m
    uint32_t totalRunTime; // 生命周期总开机运行时间 /min
    uint32_t nowRunTime; // 当次开机运行时间 /min
    uint32_t bootTimes; // 生命周期总开机次数
    uint16_t Recv; // 预留
    uint16_t AutoChargeNum; // 充电次数
    uint32_t CumulativeCap; // 累计容量 单位 0.1Ah
    uint32_t recv[9]; // 预留
};
#pragma pack(pop)

// 系统运行状态
#pragma pack(push, 1)
typedef struct {
    uint16_t Intervel; // 执行间隔
    uint32_t RunCnt; // 执行总次数
} TaskRunCnt;
typedef struct {
    struct {
        uint32_t devRun :1; //机器人运行 [0] 异常 [1] 正常
        uint32_t powerStatue :1; //机器人开关机状态（以按钮动作为准）[0] 关机 [1] 开机
        uint32_t lowPowerStatue :1; //低电量告警 [0] 正常 [1] 低电量
        uint32_t flagParamLoad :1; //参数加载结果 [0] 不成功 [1] 正常   
        uint32_t flagFlashParamLoadToRam :1; //FLASH加载结果 [0] 不成功 [1] 正常   
        uint32_t paramNeedSync :1; // 参数需要从内存中同步到Flash中 不需要 [1] 需要   
        uint32_t flagParamMasterOk :1; // 主参数是否正常 [0] 不正常 [1] 正常
        uint32_t flagParamSlaveOk :1; // 备参数是否正常 [0] 不正常 [1] 正常   
        uint32_t flagFumigating :1; // [0] 没有按下 [1] 按下
        uint32_t FlagPowerOnButtonEnter :1; // 机器人上电按钮 [0] 未按下[1] 按下 
        uint32_t FlagPowerStatus :1; // 机器人上下电完成状态 [0] 断电 [1] 上电 
        uint32_t flagEmergencyButtonEnter :1; // 机器人急停按下 [0] 未按下 [1] 按下 
        uint32_t flagEmergencyStopStatue :1; // 机器人急停状态 [0] 正常 [1] 急停 
        uint32_t flagAnticollisionBarEnable :1; // 防撞条防撞打开 [0] 关闭 [1] 打开
        uint32_t flagUltAnticollisionEnable :1; // 超声波防撞打开 [0] 关闭 [1] 打开
        uint32_t flagAnticollisionBarStatue :1; // 防撞条状态 [0] 正常 [1] 撞击
        uint32_t flagUltAnticollisionStatue :1; // 超声波防撞状态 [0] 正常 [1] 避障，紧急自动
        uint32_t flagHeartBeatNormal :1; // 心跳正常 [0] 异常 [1] 正常
        uint32_t rev18 :1; // 保留（激活检测标志）[0] 未激活 [1] 激活
        uint32_t flagSOSButtonEnter :1; // sos按键按下 [0] 未按下 [1] 按下
        uint32_t rev20 :1; // 保留（产生过急停） [0] 未产生 [1] 产生过
        uint32_t flagLaserEnable :1; // 激光悬崖避障开关 [0] 关闭 [1] 打开
        uint32_t flagDebugCommConnect :1; // 调试工具通信标志 [0] 未通信 [1] 通信正常
        uint32_t flagGsNavigationCommFlag :1; // 高仙导航通信标志 [0] 未通信 [1] 通信正常
        uint32_t flagSelfNavigationCommFlag :1; // 自研导航通信标志 [0] 未通信 [1] 通信正常
        uint32_t flagCommBeatFlag :1; // SOCKET通信标志/整个网络的通信标志 [0] 未通信 [1] 通信正常
        uint32_t rev26 :1; // 保留（是否需要掉电重启） [0] 不需要 [1] 需要
        uint32_t flagBatComStatue :1; // 电池通信是否正常 [0] 不正常 [1] 正常
        uint32_t DisableChargeDetect :1; // 是否在运动中 [0] 没运动 [1] 运动中
        uint32_t rev :3; //保留
    } StaFlag;
    uint32_t motionCmdTimeOut;
    TaskRunCnt systemRunCnt;
    TaskRunCnt obstacleRunCnt;
    TaskRunCnt attachRunCnt;
    TaskRunCnt sensorRunCnt;
    TaskRunCnt motorRunCnt;
    TaskRunCnt udp0RunCnt;
    TaskRunCnt udp1RunCnt;
    TaskRunCnt tcp4RunCnt;
    TaskRunCnt tcp5RunCnt;
    TaskRunCnt tcp6RunCnt;
    TaskRunCnt powerRunCnt;
} SystemStatus;
#pragma pack(pop)

// GPS信息
#pragma pack(push, 1)
struct GpsTime {
    uint16_t Year;   // 1970 ~ 2100
    uint8_t Month;   // 1 ~ 12
    uint8_t Day;     // 1 ~ 31
    uint8_t Hour;    // 0 ~ 23
    uint8_t Minute;  // 0 ~ 59
    uint8_t Second;  // 0 ~ 59
};
struct GpsInfo {
    uint8_t Valid; // 定位标志: 0无效, 1有效
    uint8_t EW; // 1西经, 2东经
    uint8_t SN; // 1北纬, 2南纬
    uint32_t Longitude; // 经度 单位度, 实际数据*1000000
    uint32_t Latitude; // 纬度 单位度, 实际数据*1000000
    int32_t ASL; // 海拔 单位米, 实际数据*100
    uint32_t Speed; // 相对位移速度 单位km/h, 实际数据*1000
    uint32_t Direction; // 相对位移方向 单位度, 实际数据*100
    GpsTime Time; // 时间
};
#pragma pack(pop)

class Parser {
public:
    Parser();
    ~Parser();
    void parse_battery_info(const std::vector<uint8_t>& data);
    void parse_charging_status(const std::vector<uint8_t>& data);
    void parse_system_status(const std::vector<uint8_t>& data);
    void parse_temperature_humidity(const std::vector<uint8_t>& data);
    void parse_total_runtime(const std::vector<uint8_t>& data);
    void parse_button_status(const std::vector<uint8_t>& data);
    void parse_gps(const std::vector<uint8_t>& data);
    void parse_version(const std::vector<uint8_t>& data);

private:
    zmq::context_t ctx{1};
	zmq::socket_t publisher{ctx, zmq::socket_type::pub};
    std::shared_ptr<spdlog::logger> logger;

    VersionHandler version_handler;

    void clear_old_logs(const std::string& dir, int max_day);
    void create_log(const std::string& path);

    bool check_len(const std::vector<uint8_t>& data, size_t need, const char* type_name);
    void publish(const std::string& topic, const std::string& message);
};