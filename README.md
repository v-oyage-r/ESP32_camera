# ESP32-S3 Face Camera

基于正点原子 ESP32-S3 开发板的边缘 AI 人脸检测网络摄像头项目。项目使用 ESP-IDF、FreeRTOS、ESP-DL 和 esp32-camera，在 ESP32-S3 上完成摄像头采集、本地人脸检测、结果标注、JPEG 编码、TF 卡保存、LCD 状态显示以及 HTTP 抓拍/视频流访问。


## 功能特性

- 摄像头采集 240 x 240 RGB565 图像帧。
- 基于 ESP-DL 模型库的 `HumanFaceDetectMSR01` + `HumanFaceDetectMNP01` 两阶段人脸检测。
- 在 RGB565 帧缓冲上绘制人脸框和北京时间时间戳水印。
- 将处理后的 RGB565 图像编码为 JPEG 并保存到 TF 卡。
- 根据检测结果使用 `Fxxxxxx.JPG` / `Nxxxxxx.JPG` 前缀区分有人脸和无人脸图片。
- 通过 Wi-Fi STA 连接路由器，启动 HTTP 服务。
- HTTP 支持单帧抓拍、MJPEG 视频流、已保存照片列表和照片查看。
- LCD 显示 Wi-Fi、IP、TF 卡、HTTP、人脸检测器、运行模式和最近一次操作结果。
- KEY0 在空闲模式和定时抓拍模式之间切换。
- 使用 FreeRTOS 任务拆分 UI 刷新、按键控制、定时抓拍和时间同步逻辑。

## 硬件平台

- 主控：ESP32-S3
- 开发板：正点原子 ESP32-S3 开发板
- 摄像头：OV5640 / OV3660 兼容配置
- 显示：正点原子 SPI LCD 模块
- IO 扩展：XL9555
- 存储：TF 卡，SDSPI 模式
- Flash：16 MB
- PSRAM：Octal PSRAM，80 MHz

## 软件环境

- ESP-IDF 5.4.3
- C / C++
- FreeRTOS
- ESP-DL
- esp32-camera
- esp_http_server
- esp_wifi / esp_event / esp_netif
- FATFS / sdmmc / SDSPI

## 目录结构

```text
.
├── main/
│   └── main.c                  # NVS、板级外设初始化和运行时启动入口
├── components/
│   ├── app/
│   │   ├── app_runtime.c/.h     # 运行时调度、任务、状态和抓拍链路
│   │   ├── app_camera.c/.h      # 摄像头初始化和帧缓冲获取/释放
│   │   ├── app_face.cpp/.h      # ESP-DL 人脸检测、画框和时间戳绘制
│   │   ├── app_http.c/.h        # HTTP 抓拍、MJPEG 流和照片浏览
│   │   ├── app_wifi.c/.h        # Wi-Fi STA 连接
│   │   ├── app_sdcard.c/.h      # TF 卡挂载和文件操作
│   │   └── app_key.c/.h         # KEY0~KEY3 按键封装
│   ├── BSP/                     # LCD、LED、I2C、SPI、XL9555 板级驱动
│   ├── esp32-camera/            # ESP32 摄像头驱动组件
│   └── esp-dl/                  # ESP-DL 推理组件和模型库
├── CMakeLists.txt
├── sdkconfig
├── dependencies.lock
└── partitions-16MiB.csv
```

## 系统数据流

1. `app_main()` 初始化 NVS、LED、I2C、SPI、XL9555 和 LCD。
2. `app_runtime_start()` 依次初始化 TF 卡、摄像头、Wi-Fi 和 HTTP 服务。
3. `capture_task` 在抓拍模式下每 10 秒触发一次抓拍。
4. `app_camera_capture()` 调用 `esp_camera_fb_get()` 获取 RGB565 帧缓冲。
5. `app_face_detect_rgb565()` 使用 MSR01 生成候选框，再由 MNP01 进行二阶段检测。
6. 若检测到人脸，在原始 RGB565 帧缓冲上绘制人脸框，并叠加时间戳。
7. `fmt2jpg_cb()` 将 RGB565 图像编码为 JPEG，保存到 TF 卡。
8. 处理完成后调用 `esp_camera_fb_return()` 归还摄像头帧缓冲。
9. HTTP 服务提供 `/capture`、`/stream`、`/photos` 和 `/photo` 接口供浏览器访问。

## FreeRTOS 任务与同步

### 任务拆分

- `ui_task`：每秒刷新 LCD 状态，同时在 Wi-Fi 就绪后周期性触发时间同步。
- `control_task`：每 30 ms 扫描按键，KEY0 用于切换空闲/抓拍模式。
- `capture_task`：在抓拍模式下按 10 秒间隔执行采集、检测、绘制、编码、保存和旧图清理。
- `time_task`：每 10 分钟执行一次 SNTP 时间同步。

### 事件组

`app_runtime.c` 中的 `s_app_evt` 用于记录系统运行状态：

- `APP_EVT_WIFI_READY`
- `APP_EVT_SD_READY`
- `APP_EVT_TIME_SYNCED`
- `APP_EVT_CAPTURE_MODE`
- `APP_EVT_HTTP_READY`

`app_wifi.c` 中的 `s_wifi_event_group` 用于 Wi-Fi 连接等待：

- `WIFI_CONNECTED_BIT`
- `WIFI_FAIL_BIT`

### 互斥锁

- `s_status_mutex`：保护运行时状态结构体 `s_status`。
- `s_camera_mutex`：保护定时抓拍链路中的相机抓帧过程。
- `s_sd_mutex`：保护 JPEG 保存和旧照片清理过程。

注意：HTTP `/capture` 和 `/stream` 路径会直接调用相机抓帧接口，当前没有复用 `app_runtime.c` 中的 `s_camera_mutex`。

## HTTP 接口

设备连接 Wi-Fi 后，LCD 会显示 IP 地址。浏览器访问：

```text
http://<device-ip>/
```

可用接口：

- `/`：首页
- `/capture`：获取当前单帧 JPEG 抓拍
- `/stream`：MJPEG 视频流
- `/photos`：查看 TF 卡中保存的 JPG 文件列表
- `/photo?name=<filename>.JPG`：查看指定照片

## 配置说明

Wi-Fi 名称和密码当前在 `components/app/app_runtime.c` 中通过宏配置：

```c
#define WIFI_ID "xxxxx"
#define WIFI_PWD "Your_Password"
```

摄像头参数在 `components/app/app_camera.c` 中配置：

```c
.xclk_freq_hz = 24000000,
.pixel_format = PIXFORMAT_RGB565,
.frame_size = FRAMESIZE_240X240,
.fb_count = 1,
```

抓拍和清理参数在 `components/app/app_runtime.c` 中配置：

```c
#define APP_CAPTURE_INTERVAL_MS     10000
#define APP_IMAGE_KEEP_SECONDS      (2 * 60 * 60)
```

## 构建与烧录

确认已安装并导出 ESP-IDF 环境变量后执行：

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

工程使用自定义 16 MB 分区表：

```text
partitions-16MiB.csv
```

## 使用步骤

1. 修改 `WIFI_ID` 和 `WIFI_PWD`。
2. 插入 TF 卡。
3. 构建并烧录固件。
4. 打开串口监视器或查看 LCD，确认 Wi-Fi、SD、HTTP 状态。
5. 在浏览器访问 LCD 显示的设备 IP。
6. 按 KEY0 进入定时抓拍模式，再按一次退出。