#include <Arduino.h>
#include "base64.h"            // Base64编码库
#include "WiFi.h"              // WiFi功能库
#include <WiFiClientSecure.h>  // 安全的WiFi客户端库
#include "HTTPClient.h"        // HTTP客户端库
#include "Audio1.h"            // 自定义的音频处理库1
#include "Audio2.h"            // 自定义的音频处理库2
#include <ArduinoJson.h>       // ArduinoJson库，用于处理JSON数据
#include <ArduinoWebsockets.h> // WebSocket通信库
using namespace websockets;    // 使用WebSocket命名空间

#define key 0
#define ADC 32
#define led3 2
#define led2 18
#define led1 19
const char *wifiData[][2] = {
    {"LYQ", "37628245"}, // 替换为自己常用的wifi名和密码
    {"OnePlus Ace Pro", "28081963"},
    // 继续添加需要的 Wi-Fi 名称和密码
};

// 定义讯飞API的APPID、APIKey和APISecret
String APPID = "42bb3912";
String APIKey = "475fd863126e35fee8fefb26201d2f62";
String APISecret = "N2E1ODFiMGYzMzQzNWJkNzJjMWVhOTZm";

// 定义程序状态标志
bool ledstatus = true;
bool startPlay = false;
bool lastsetence = false;
bool isReady = false;
unsigned long urlTime = 0;
unsigned long pushTime = 0;
int mainStatus = 0;
int receiveFrame = 0;
int noise = 50;

HTTPClient https;         // 创建HTTPClient对象
hw_timer_t *timer = NULL; // 硬件定时器
uint8_t adc_start_flag = 0;
uint8_t adc_complete_flag = 0;
Audio1 audio1;                      // 创建Audio1对象
Audio2 audio2(false, 3, I2S_NUM_1); // 创建Audio2对象

#define I2S_DOUT 25 // I2S数据引脚
#define I2S_BCLK 27 // I2S时钟引脚
#define I2S_LRC 26  // I2S左右声道引脚

// 声明函数
void gain_token(void);
void getText(String role, String content);           // 处理文本
void checkLen(JsonArray textArray);                  // 检查textArray数组的长度并删除超出3000个字符的部分
int getLength(JsonArray textArray);                  // 获取textArray数组长度
float calculateRMS(uint8_t *buffer, int bufferSize); // 计算音频信号均方根值
void ConnServer();                                   // 连接到星火大模型服务器
void ConnServer1();                                  // 连接到语音识别服务服务器

// 定义JSON文档和数组
DynamicJsonDocument doc(4000);
JsonArray text = doc.to<JsonArray>();

String url = "";                                                       // WebSocket URL
String url1 = "";                                                      // WebSocket URL1
String Date = "";                                                      // 服务器时间
DynamicJsonDocument gen_params(const char *appid, const char *domain); // 接口请求参数生成

String askquestion = ""; // 存储问题
String Answer = "";      // 存储回答

const char *appId1 = "42bb3912";                                       // 星火大模型参数
const char *domain1 = "generalv3";                                     // 星火大模型领域
const char *websockets_server = "ws://spark-api.xf-yun.com/v3.1/chat"; // WebSocket服务器URL
const char *websockets_server1 = "ws://ws-api.xfyun.cn/v2/iat";        // WebSocket服务器URL1

using namespace websockets;

// 创建WebSocket客户端对象
WebsocketsClient webSocketClient;
WebsocketsClient webSocketClient1;

int loopcount = 0;

// 当从服务器接收到WebSocket消息时调用此函数
void onMessageCallback(WebsocketsMessage message)
{
    // 创建一个静态JSON文档，用于解析接收到的消息
    StaticJsonDocument<4096> jsonDocument;
    // 反序列化接收到的消息，将其解析为JSON对象
    DeserializationError error = deserializeJson(jsonDocument, message.data());

    // 如果解析没有错误
    if (!error)
    {
        // 获取响应头中的code字段
        int code = jsonDocument["header"]["code"];

        // 如果code不为0，表示有错误发生
        if (code != 0)
        {
            Serial.print("sth is wrong: ");
            Serial.println(code);
            Serial.println(message.data());
            // 关闭WebSocket连接
            webSocketClient.close();
        }

        // 如果没有错误，处理接收到的数据
        else
        {
            // 增加接收到的帧数
            receiveFrame++;
            Serial.print("receiveFrame:");
            Serial.println(receiveFrame);

            // 获取payload中的choices字段
            JsonObject choices = jsonDocument["payload"]["choices"];
            // 获取语音识别的状态
            int status = choices["status"];
            // 获取识别出的文本内容
            const char *content = choices["text"][0]["content"];
            Serial.println(content);

            // 将内容添加到Answer字符串中
            Answer += content;
            String answer = "";

            // 如果Answer长度大于等于120且不在播放语音
            if (Answer.length() >= 120 && (audio2.isplaying == 0))
            {
                // 获取Answer的前120个字符作为子答案
                String subAnswer = Answer.substring(0, 120);
                Serial.print("subAnswer:");
                Serial.println(subAnswer);

                // 查找最后一个句号的位置
                int lastPeriodIndex = subAnswer.lastIndexOf("。");

                // 如果找到句号，截取到句号后一位
                if (lastPeriodIndex != -1)
                {
                    answer = Answer.substring(0, lastPeriodIndex + 1);
                    Serial.print("answer: ");
                    Serial.println(answer);
                    // 截取剩余的Answer
                    Answer = Answer.substring(lastPeriodIndex + 2);
                    Serial.print("Answer: ");
                    Serial.println(Answer);
                    // 将子答案发送到语音合成
                    audio2.connecttospeech(answer.c_str(), "zh");
                }

                // 如果没有找到句号，处理中文标点
                else
                {
                    const char *chinesePunctuation = "？，：；,.";
                    int lastChineseSentenceIndex = -1;

                    // 遍历子答案中的每个字符，查找中文标点
                    for (int i = 0; i < Answer.length(); ++i)
                    {
                        char currentChar = Answer.charAt(i);

                        if (strchr(chinesePunctuation, currentChar) != NULL)
                        {
                            lastChineseSentenceIndex = i;
                        }
                    }

                    // 如果找到中文标点，截取到标点后一位
                    if (lastChineseSentenceIndex != -1)
                    {
                        answer = Answer.substring(0, lastChineseSentenceIndex + 1);
                        audio2.connecttospeech(answer.c_str(), "zh");
                        Answer = Answer.substring(lastChineseSentenceIndex + 2);
                    }

                    // 如果没有找到标点，截取前120个字符并发送
                    else
                    {
                        answer = Answer.substring(0, 120);
                        audio2.connecttospeech(answer.c_str(), "zh");
                        Answer = Answer.substring(120 + 1);
                    }
                }
                // 设置开始播放标志
                startPlay = true;
            }

            // 如果状态为2，表示服务器发送的最后一条消息
            if (status == 2)
            {
                getText("assistant", Answer);
                if (Answer.length() <= 80 && (audio2.isplaying == 0))
                {
                    // getText("assistant", Answer);
                    audio2.connecttospeech(Answer.c_str(), "zh");
                }
            }
        }
    }
}

// 当WebSocket发生事件时调用此函数
void onEventsCallback(WebsocketsEvent event, String data)
{
    // 如果事件是连接打开
    if (event == WebsocketsEvent::ConnectionOpened)
    {
        Serial.println("Send message to server0!");
        // 生成JSON参数
        DynamicJsonDocument jsonData = gen_params(appId1, domain1);
        String jsonString;
        // 将JSON对象序列化为字符串
        serializeJson(jsonData, jsonString);
        Serial.println(jsonString);
        // 发送JSON字符串到服务器
        webSocketClient.send(jsonString);
    }

    // 判断对方死活
    else if (event == WebsocketsEvent::ConnectionClosed)
    {
        Serial.println("Connnection0 Closed");
    }
    else if (event == WebsocketsEvent::GotPing)
    {
        Serial.println("Got a Ping!");
    }
    else if (event == WebsocketsEvent::GotPong)
    {
        Serial.println("Got a Pong!");
    }
}

// 当从语音识别服务器接收到WebSocket消息时调用此函数
void onMessageCallback1(WebsocketsMessage message)
{
    // 创建一个JSON文档用于解析消息
    StaticJsonDocument<4096> jsonDocument;
    DeserializationError error = deserializeJson(jsonDocument, message.data());

    // 如果解析没有错误
    if (!error)
    {
        int code = jsonDocument["code"];
        if (code != 0)
        {
            Serial.println(code);
            Serial.println(message.data());
            // 关闭WebSocket连接
            webSocketClient1.close();
        }
        else
        {
            // 打印来自讯飞的消息
            Serial.println("xunfeiyun return message:");
            Serial.println(message.data());

            // 获取语音识别结果的数组
            JsonArray ws = jsonDocument["data"]["result"]["ws"].as<JsonArray>();

            // 遍历数组，将每个识别出的词汇添加到askquestion字符串中
            for (JsonVariant i : ws)
            {
                for (JsonVariant w : i["cw"].as<JsonArray>())
                {
                    askquestion += w["w"].as<String>(); // 得到对语音识别的结果
                }
            }

            // 打印识别出的文本
            Serial.println(askquestion);
            // 获取语音识别的状态
            int status = jsonDocument["data"]["status"];

            // 如果状态为2，表示服务器发送的最后一条消息
            if (status == 2)
            {
                Serial.println("status == 2");
                // 关闭WebSocket连接
                webSocketClient1.close();

                // 如果没有识别到内容，则播放默认文本
                if (askquestion == "")
                {
                    askquestion = "对不起，我没听懂"; // 表示服务器识别不到你的话
                    audio2.connecttospeech(askquestion.c_str(), "zh");
                }

                // 如果识别到的内容是“唱歌了”或“唱歌啦”，根据内容播放对应的歌曲
                else if (askquestion.substring(0, 9) == "唱首歌")
                {
                    if (askquestion.substring(0, 12) == "唱首歌，")
                    { // 自建音乐服务器，按照文件名查找对应歌曲
                        // 构建歌曲的URL
                        String audioStreamURL = "http://106.52.19.107/home/ubuntu/music/" + askquestion.substring(12, askquestion.length() - 3) + ".mp3 ";
                        Serial.println(audioStreamURL.c_str());
                        // 连接到音乐服务器并播放音乐
                        audio2.connecttohost(audioStreamURL.c_str()); // 重复？？？？？？？？？？？？？
                    }

                    // 提问用户想听什么歌
                    else if (askquestion.substring(9) == "。")
                    {
                        askquestion = "好啊, 你想听什么歌？";
                        mainStatus = 1;
                        audio2.connecttospeech(askquestion.c_str(), "zh");
                    }

                    else
                    {
                        String audioStreamURL = "http://106.52.19.107/home/ubuntu/music/" + askquestion.substring(9, askquestion.length() - 3) + ".mp3";
                        Serial.println(audioStreamURL.c_str());
                        // 连接到音乐服务器并播放音乐
                        audio2.connecttohost(audioStreamURL.c_str()); // 重复？？？？？？？？？？？
                    }
                }

                // 如果之前的状态是请求歌曲名称
                else if (mainStatus == 1)
                {
                    askquestion.trim(); // 去除字符串两端的空白字符

                    // 去除结尾的“。”
                    if (askquestion.endsWith("。"))
                    {
                        askquestion = askquestion.substring(0, askquestion.length() - 3);
                    }

                    // 去除结尾的“.”或“?”
                    else if (askquestion.endsWith(".") or askquestion.endsWith("?"))
                    {
                        askquestion = askquestion.substring(0, askquestion.length() - 1);
                    }
                    // 构建歌曲的URL
                    String audioStreamURL = "http://192.168.0.1/mp3/" + askquestion + ".mp3";
                    Serial.println(audioStreamURL.c_str());
                    // 连接到音乐服务器并播放音乐
                    audio2.connecttohost(audioStreamURL.c_str());
                    // 重置状态
                    mainStatus = 0;
                }
                else // 如果非唱歌，即与大模型的常规对话
                {
                    getText("user", askquestion); // 处理用户的问题
                    Serial.print("text:");
                    Serial.println(text);
                    Answer = "";         // 准备接收大模型的回话
                    lastsetence = false; // 设置最后一个句子的标志为false
                    isReady = true;      // 设置准备好的标志为tru
                    ConnServer();        // 连接大模型
                }
            }
        }
    }

    // 如果解析JSON时发生错误
    else
    {
        Serial.println("error:");
        Serial.println(error.c_str());
        Serial.println(message.data());
    }
}

// 当从语音识别服务器接收到WebSocket事件时调用此函数
void onEventsCallback1(WebsocketsEvent event, String data)
{
    // 如果事件是连接打开
    if (event == WebsocketsEvent::ConnectionOpened) // 如果与服务器连接成功
    {
        Serial.println("Send message to xunfeiyun");
        digitalWrite(led2, HIGH); // 点亮LED2表示正在发送数据

        // 初始化一些变量用于语音检测
        int silence = 0;
        int firstframe = 1; // 第一帧向服务器送的数据
        int j = 0;
        int voicebegin = 0;
        int voice = 0;

        DynamicJsonDocument doc(2500); // 创建一个JSON文档用于发送数据
        while (1)                      // 利用websocket建立持续的连接，能不断将语音信息送入大模型，边录边送
        {
            doc.clear();                                      // 清除文档以便重新使用
            JsonObject data = doc.createNestedObject("data"); // 用json进行信息规范化
            audio1.Record();
            float rms = calculateRMS((uint8_t *)audio1.wavData[0], 1280); // 计算平均音量值，检查是否采集到声音
            printf("%d %f\n", 0, rms);
            if (rms < noise) // 如果RMS值小于噪声阈值，表示没有检测到声音
            {
                if (voicebegin == 1)
                {
                    silence++; // 如果已经开始说话，增加静音计数
                    // Serial.print("noise:");
                    // Serial.println(noise);
                }
            }
            else // 如果RMS值大于噪声阈值，表示检测到声音
            {
                voice++; // 增加声音计数0.1s加一次

                // 若0.5秒都有声音，则标记为开始说话
                if (voice >= 5)
                {
                    voicebegin = 1;
                }

                // 否则，标记为未开始说话
                else
                {
                    voicebegin = 0;
                }
                silence = 0;
            }

            // 如果连续静音达到6次（0.6秒），发送最后一包数据
            if (silence == 6)
            {
                data["status"] = 2;                                              // 设置状态为2表示最后一包数据
                data["format"] = "audio/L16;rate=8000";                          // 设置音频格式
                data["audio"] = base64::encode((byte *)audio1.wavData[0], 1280); // 对音频数据进行Base64编码
                data["encoding"] = "raw";                                        // 设置编码方式为raw
                j++;

                String jsonString;              // 创建一个字符串用于发送
                serializeJson(doc, jsonString); // 将JSON文档序列化为字符串

                // 发送JSON字符串到服务器
                webSocketClient1.send(jsonString);
                digitalWrite(led2, LOW);
                delay(40);
                break;
            }

            // 如果是第一帧数据
            if (firstframe == 1)
            {
                data["status"] = 0;                                              // 设置状态为0
                data["format"] = "audio/L16;rate=8000";                          // 设置音频格式
                data["audio"] = base64::encode((byte *)audio1.wavData[0], 1280); // 对音频数据进行Base64编码
                data["encoding"] = "raw";                                        // 设置编码方式为raw
                j++;

                JsonObject common = doc.createNestedObject("common"); // 创建一个common对象
                common["app_id"] = appId1;                            // 设置app_id

                JsonObject business = doc.createNestedObject("business"); // 创建一个business

                business["domain"] = "iat";      // 设置领域为iat
                business["language"] = "zh_cn";  // 设置语言为中文
                business["accent"] = "mandarin"; // 设置口音为普通话
                business["vinfo"] = 1;           // 设置vinfo为1
                business["vad_eos"] = 1000;      // 设置vad_eos为1000

                String jsonString;              // 创建一个字符串用于发送
                serializeJson(doc, jsonString); // 创建一个字符串用于发送

                // 发送JSON字符串到服务器
                webSocketClient1.send(jsonString);
                firstframe = 0; // 标记已发送第一帧
                delay(40);
            }

            // 如果不是第一帧
            else
            {
                data["status"] = 1;                                              // 设置状态为1，表示中间帧
                data["format"] = "audio/L16;rate=8000";                          // 设置音频格式
                data["audio"] = base64::encode((byte *)audio1.wavData[0], 1280); // 将音频数据编码为Base64
                data["encoding"] = "raw";                                        // 设置编码方式为raw

                String jsonString;              // 创建一个字符串用于发送
                serializeJson(doc, jsonString); // 将JSON文档序列化为字符串

                // 发送JSON字符串到服务器
                webSocketClient1.send(jsonString);
                delay(40);
            }
        }
    }
    else if (event == WebsocketsEvent::ConnectionClosed)
    {
        Serial.println("Connnection1 Closed");
    }
    else if (event == WebsocketsEvent::GotPing)
    {
        Serial.println("Got a Ping!");
    }
    else if (event == WebsocketsEvent::GotPong)
    {
        Serial.println("Got a Pong!");
    }
}

// 连接到星火大模型服务器的函数
void ConnServer()
{
    Serial.println("url:" + url);
    // 设置WebSocket消息回调函数（接收大模型处理后的文本，并拼接回答）
    webSocketClient.onMessage(onMessageCallback);
    // 设置WebSocket事件回调函数
    webSocketClient.onEvent(onEventsCallback);

    Serial.println("Begin connect to server0......");
    if (webSocketClient.connect(url.c_str()))
    {
        Serial.println("Connected to server0!");
    }
    else
    {
        Serial.println("Failed to connect to server0!");
    }
}

// 连接到语音识别服务服务器的函数
void ConnServer1()
{
    // Serial.println("url1:" + url1);

    // 设置WebSocket消息回调函数
    webSocketClient1.onMessage(onMessageCallback1); // 如果服务器返回了消息，进入回调函数（处理服务器返回的数据，并开始对话大模型）
    // 设置WebSocket事件回调函数
    webSocketClient1.onEvent(onEventsCallback1); // 如果服务器返回了事件，进入回调函数（发送语音数据）

    Serial.println("Begin connect to server1......");
    if (webSocketClient1.connect(url1.c_str())) // url连接大模型,url1连接语音服务
    {
        Serial.println("Connected to server1!");
    }
    else
    {
        Serial.println("Failed to connect to server1!");
    }
}

// 播放语音
void voicePlay()
{
    // 如果扬声器当前没有在播放语音，并且Answer字符串中有文本
    if ((audio2.isplaying == 0) && Answer != "")
    {
        // String subAnswer = "";
        // String answer = "";
        // if (Answer.length() >= 100)
        //     subAnswer = Answer.substring(0, 100);
        // else
        // {
        //     subAnswer = Answer.substring(0);
        //     lastsetence = true;
        //     // startPlay = false;
        // }

        // Serial.print("subAnswer:");
        // Serial.println(subAnswer);

        // 尝试找到Answer中第一个句号的位置
        int firstPeriodIndex = Answer.indexOf("。");
        int secondPeriodIndex = 0;

        // 如果找到了句号，尝试找到第二个句号的位置
        if (firstPeriodIndex != -1)
        {
            secondPeriodIndex = Answer.indexOf("。", firstPeriodIndex + 1);
            // 如果没有找到第二个句号，将第二个位置设为第一个句号的位置
            if (secondPeriodIndex == -1)
                secondPeriodIndex = firstPeriodIndex;
        }
        else
        {
            // 如果没有找到句号，将两个位置都设置为-1
            secondPeriodIndex = firstPeriodIndex;
        }

        // 如果找到了第二个句号的位置
        if (secondPeriodIndex != -1)
        {
            // 截取从开始到第二个句号后一位的字符串作为答案
            String answer = Answer.substring(0, secondPeriodIndex + 1);
            Serial.print("answer: ");
            Serial.println(answer);
            // 截取剩余的字符串
            Answer = Answer.substring(secondPeriodIndex + 2);
            // 调用语音合成函数，将答案转换为语音播放
            audio2.connecttospeech(answer.c_str(), "zh");
        }

        // 如果没有找到句号，处理中文标点
        else
        {
            const char *chinesePunctuation = "？，：；,.";
            int lastChineseSentenceIndex = -1;

            // 遍历Answer中的每个字符，查找中文标点
            for (int i = 0; i < Answer.length(); ++i)
            {
                char currentChar = Answer.charAt(i);
                // 如果找到中文标点，记录位置
                if (strchr(chinesePunctuation, currentChar) != NULL)
                {
                    lastChineseSentenceIndex = i;
                }
            }

            // 如果找到中文标点
            if (lastChineseSentenceIndex != -1)
            {
                // 截取从开始到中文标点后一位的字符串作为答案
                String answer = Answer.substring(0, lastChineseSentenceIndex + 1);
                audio2.connecttospeech(answer.c_str(), "zh");
                // 截取剩余的字符串
                Answer = Answer.substring(lastChineseSentenceIndex + 2);
            }
        }
        // 设置开始播放标志为true
        startPlay = true;
    }
    else
    {
        // digitalWrite(led3, LOW);
    }
}

// 连接到WiFi网络函数
void wifiConnect(const char *wifiData[][2], int numNetworks)
{
    // 断开现有的WiFi连接
    WiFi.disconnect(true);
    for (int i = 0; i < numNetworks; ++i)
    {
        const char *ssid = wifiData[i][0];     // WiFi名称
        const char *password = wifiData[i][1]; // WiFi密码

        Serial.print("Connecting to ");
        Serial.println(ssid);

        WiFi.begin(ssid, password); // 开始连接到WiFi
        uint8_t count = 0;

        // 等待WiFi连接成功
        while (WiFi.status() != WL_CONNECTED)
        {
            // 切换LED状态表示正在尝试连接
            digitalWrite(led1, ledstatus);
            ledstatus = !ledstatus;
            Serial.print(".");
            count++;

            // 如果尝试了30秒还没有连接成功，则断开连接尝试下一个网络
            if (count >= 30)
            {
                Serial.printf("\r\n-- wifi connect fail! --");
                break;
            }
            vTaskDelay(100);
        }

        // 如果连接成功
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.printf("\r\n-- wifi connect success! --\r\n");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());                            // 打印分配给ESP32的IP地址
            Serial.println("Free Heap: " + String(ESP.getFreeHeap())); // 打印剩余可用内存
            return;                                                    // 如果连接成功，退出函数
        }
    }
}

// 本地鉴权
String getUrl(String Spark_url, String host, String path, String Date)
{

    // 拼接字符串
    String signature_origin = "host: " + host + "\n";
    signature_origin += "date: " + Date + "\n";
    signature_origin += "GET " + path + " HTTP/1.1";
    // signature_origin="host: spark-api.xf-yun.com\ndate: Mon, 04 Mar 2024 19:23:20 GMT\nGET /v3.5/chat HTTP/1.1";

    // hmac-sha256 加密
    unsigned char hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    const size_t messageLength = signature_origin.length();
    const size_t keyLength = APISecret.length();
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)APISecret.c_str(), keyLength);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)signature_origin.c_str(), messageLength);
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    // base64 编码
    String signature_sha_base64 = base64::encode(hmac, sizeof(hmac) / sizeof(hmac[0]));

    // 替换Date
    Date.replace(",", "%2C");
    Date.replace(" ", "+");
    Date.replace(":", "%3A");
    String authorization_origin = "api_key=\"" + APIKey + "\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"" + signature_sha_base64 + "\"";
    String authorization = base64::encode(authorization_origin);
    String url = Spark_url + '?' + "authorization=" + authorization + "&date=" + Date + "&host=" + host;
    Serial.println(url);
    return url;
}

// 从服务器获取时间
void getTimeFromServer()
{
    String timeurl = "https://www.baidu.com"; // 请求的URL
    HTTPClient http;                          // 创建HTTPClient对象
    http.begin(timeurl);
    const char *headerKeys[] = {"Date"}; // 初始化HTTP请求

    // 设置需要收集的响应头
    http.collectHeaders(headerKeys, sizeof(headerKeys) / sizeof(headerKeys[0]));
    int httpCode = http.GET(); // 发送GET请求

    // 如果请求成功
    if (httpCode > 0)
    {
        // 获取响应头中的Date字段
        Date = http.header("Date");
        Serial.println(Date); // 打印获取到的时间
    }
    else
    {
        Serial.println("Error on HTTP request"); // 如果请求失败，打印错误信息
    }
    http.end(); // 关闭HTTP连接
    // delay(50); // 可以根据实际情况调整延时时间
}

// 初始化
void setup()
{
    // String Date = "Fri, 22 Mar 2024 03:35:56 GMT";
    Serial.begin(115200);
    // pinMode(ADC,ANALOG);
    pinMode(key, INPUT_PULLUP);
    pinMode(34, INPUT_PULLUP);
    pinMode(35, INPUT_PULLUP);
    pinMode(led1, OUTPUT);
    pinMode(led2, OUTPUT);
    pinMode(led3, OUTPUT);
    audio1.init(); // 麦克风

    int numNetworks = sizeof(wifiData) / sizeof(wifiData[0]); // 计算WiFi网络数量
    wifiConnect(wifiData, numNetworks);                       // 依次尝试连接wifi节点

    getTimeFromServer(); // 获取服务器时间

    audio2.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT); // 扬声器
    audio2.setVolume(50);                          // 设置扬声器的音量为50

    // String Date = "Fri, 22 Mar 2024 03:35:56 GMT";
    url = getUrl("ws://spark-api.xf-yun.com/v3.1/chat", "spark-api.xf-yun.com", "/v3.1/chat", Date); // 本地生成WebSocket URL
    url1 = getUrl("ws://ws-api.xfyun.cn/v2/iat", "ws-api.xfyun.cn", "/v2/iat", Date);                // 本地生成语音识别服务的WebSocket URL
    urlTime = millis();                                                                              // // 记录当前时间，用于后续鉴权，超过5分钟鉴权失效

    ///////////////////////////////////
}

// 主循环
void loop()
{
    webSocketClient.poll();  // 与大模型建立webSocket持续连接，推动其执行
    webSocketClient1.poll(); // 与语音服务建立webSocket持续连接，推动其执行
    // delay(10);

    // 如果需要播放语音，调用voicePlay函数
    if (startPlay)
    {
        voicePlay();
    }

    // 播放声音主循环
    audio2.loop();

    // 如果正在播放语音，点亮LED3
    if (audio2.isplaying == 1)
    {
        digitalWrite(led3, HIGH);
    }
    // 如果没有播放语音，熄灭LED3
    else
    {
        digitalWrite(led3, LOW);
        // 如果过去4分钟并且扬声器不在播放，重新进行鉴权
        if ((urlTime + 240000 < millis()) && (audio2.isplaying == 0))
        {
            urlTime = millis();                                                                              // 更新urlTime
            getTimeFromServer();                                                                             // 获取服务器时间
            url = getUrl("ws://spark-api.xf-yun.com/v3.1/chat", "spark-api.xf-yun.com", "/v3.1/chat", Date); // 更新WebSocket URL
            url1 = getUrl("ws://ws-api.xfyun.cn/v2/iat", "ws-api.xfyun.cn", "/v2/iat", Date);                // 更新WebSocket URL1
        }
    }

    if (digitalRead(key) == 0) // 按下boot键
    {
        audio2.isplaying = 0;
        startPlay = false;
        isReady = false;
        Answer = "";
        Serial.printf("Start recognition\r\n\r\n");

        adc_start_flag = 1;
        // Serial.println(esp_get_free_heap_size());

        if (urlTime + 240000 < millis()) // 超过4分钟，重新做一次鉴权
        {
            urlTime = millis();
            getTimeFromServer();
            url = getUrl("ws://spark-api.xf-yun.com/v3.1/chat", "spark-api.xf-yun.com", "/v3.1/chat", Date);
            url1 = getUrl("ws://ws-api.xfyun.cn/v2/iat", "ws-api.xfyun.cn", "/v2/iat", Date);
        }
        askquestion = "";
        // audio2.connecttospeech(askquestion.c_str(), "zh");
        ConnServer1(); // 录音对接大模型开始启动
        // delay(6000);
        // audio1.Record();
        adc_complete_flag = 0;

        // Serial.println(text);
        // checkLen(text);
    }
}

// 处理文本
void getText(String role, String content)
{
    // 检查text数组的长度并删除超出长度的文本
    checkLen(text);

    // 创建一个动态JSON文档
    DynamicJsonDocument jsoncon(1024);
    // 设置role键的值为传入的role参数
    jsoncon["role"] = role;
    // 设置content键的值为传入的content参数
    jsoncon["content"] = content;
    // 将创建的JSON对象添加到text数组中
    text.add(jsoncon);
    // 清除jsoncon对象，以便可以重新使用
    jsoncon.clear();
    // 将text数组序列化为JSON字符串
    String serialized;
    serializeJson(text, serialized);
    Serial.print("text: ");
    Serial.println(serialized);
    // serializeJsonPretty(text, Serial);   // 如果需要格式化输出，可以取消注释这行
}

// 获取textArray数组长度的函数
int getLength(JsonArray textArray)
{
    int length = 0;

    // 遍历textArray数组
    for (JsonObject content : textArray)
    {
        // 获取content对象中的content键对应的值
        const char *temp = content["content"];
        // 计算字符串长度，并累加到length变量中
        int leng = strlen(temp);
        length += leng;
    }
    // 返回累加后的长度
    return length;
}

// 检查textArray数组的长度并删除超出3000个字符的部分
void checkLen(JsonArray textArray)
{
    // 当数组的总长度超过3000个字符时，删除数组的第一个元素
    while (getLength(textArray) > 3000)
    {
        textArray.remove(0);
    }
    // return textArray;
}

// 生成发送给服务器的参数的函数（参见文档https://www.xfyun.cn/doc/spark/Web.html#_1-%E6%8E%A5%E5%8F%A3%E8%AF%B4%E6%98%8E）
DynamicJsonDocument gen_params(const char *appid, const char *domain)
{
    // 创建一个2048字节大小的动态JSON文档
    DynamicJsonDocument data(2048);

    // 在data文档中创建一个嵌套的JSON对象header
    JsonObject header = data.createNestedObject("header");
    // 设置header对象的app_id键的值为传入的appid参数
    header["app_id"] = appid;
    // 设置header对象的uid键的值为"1234"
    header["uid"] = "1234";

    // 在data文档中创建一个嵌套的JSON对象parameter
    JsonObject parameter = data.createNestedObject("parameter");
    // 在parameter对象中创建一个嵌套的JSON对象chat
    JsonObject chat = parameter.createNestedObject("chat");

    chat["domain"] = domain;   // 指定访问的领域:
    chat["temperature"] = 0.5; // 核采样阈值
                               // 用于决定结果随机性，取值越高随机性越强即相同的问题得到的不同答案的可能性越高
    chat["max_tokens"] = 100;  // 模型回答的tokens的最大长度
                               // Pro、Max、Max-32K、4.0 Ultra 取值为[1,8192]，默认为4096;

    // 在data文档中创建一个嵌套的JSON对象payload
    JsonObject payload = data.createNestedObject("payload");
    // 在payload对象中创建一个嵌套的JSON对象message
    JsonObject message = payload.createNestedObject("message");

    // 在message对象中创建一个文本数组
    JsonArray textArray = message.createNestedArray("text");
    // 遍历全局text数组，并将每个元素添加到textArray中
    for (const auto &item : text)
    {
        textArray.add(item);
    }
    // 返回填充好的JSON文档
    return data;
}

// 计算音频信号均方根值的函数
float calculateRMS(uint8_t *buffer, int bufferSize)
{
    float sum = 0;
    int16_t sample; // 定义一个16位整数变量，用于存储音频样本

    // 遍历音频缓冲区，步长为2，因为每个样本由两个字节组成
    for (int i = 0; i < bufferSize; i += 2)
    {
        // 将两个字节组合成一个16位的样本
        sample = (buffer[i + 1] << 8) | buffer[i];
        // 将样本的平方累加到总和中
        sum += sample * sample;
    }
    // 计算总和的平均值
    sum /= (bufferSize / 2);
    // 返回均方根值
    return sqrt(sum);
}
