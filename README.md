# 暖宅
一个智能家居项目

# 项目架构
见docs目录中的架构图

# 代码结构
doc - 文档  
embedded_apps - 嵌入式应用层  
embedded_backend - 嵌入式后端  
embedded_common - 嵌入式公共代码  
embedded_drivers - 嵌入式驱动  
web_frontend - web前端页面

# 项目技术栈
嵌入式服务端
- C语言 mongoose cjson 消息队列 共享内存  

嵌入式应用层  
- C语言 多线程 消息队列 共享内存 视频流  

嵌入式驱动层  
- C语言 Zigbee I2C ADC GPIO UART  

前端
- react antd vite axios  

后端
- java spring  

# 问题总结
### cgi中使用JSON
安装cjson

### cgi中的缺少引用也不会报错，只会变成502看不出是什么引起
把所有警告弄好

### cgi中不能放while无限循环去接收消息
让前端去轮询

### cgi中无法使用websoket，给前端推送环境信息，且前端不是由cgi实现
使用轮询方式，后续优化将cgi换掉

### JSON通信响应格式不统一
创建统一格式的函数

### 各个端需要数据格式不一样
由服务端cgi层 app层 去转格式，前端不转，驱动层不转

### 硬件控制页，首次进去看不到硬件的状态，不知道硬件开了没
使用管道

### 编译器不同，对c99语法的支持会有问题
全部c代码都不使用随用随定义，c++代码可以写

### embedded_backend改造
使用web服务器路由

### 重复包含问题
用一个.h文件，全部放里面，不再拆分

### 多线程参数内容被回收问题
malloc

### 消息队列中传字符串问题
使用字符数组

### 字符串判断不能用 == 不像java和js
使用strcmp()

### 没有设备文件问题
设备树匹配必须精确：compatible字符串必须完全一致，包括空格、大小写等
调试方法很重要：通过在probe函数开始添加打印，我们快速定位了问题是"probe函数没被调用"
查看设备树内容：可以用 cat /proc/device-tree/节点名/compatible 来确认实际的字符串内容

以后遇到类似问题时，可以：查看设备树中的实际compatible字符串
```shell
cat /proc/device-tree/fs4412-leds/compatible
# 或者用hexdump查看（能看到空格等特殊字符）
hexdump -C /proc/device-tree/fs4412-leds/compatible
```

### 打印报段错误
声明的类型和实现的地方不一致，普通模式不会报错，但是运行会报段错误
-Wextra -Werror

### 头文件空参数也报找不到
给头文件括号里放个void

### c语言缺少一些方便的库
- 打印结构体
- 打印森林数组
- 缺少字符串截取函数

### 待解决
视频清晰度低

排查共享内存是不是没东西

增加数据库
