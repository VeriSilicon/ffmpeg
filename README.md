# Content
* [1.Introducing](#1.Introducing)
    * [Plugin Summary](#Plugin-Summary)
    * [Install VPE driver](#Install-VPE-driver)
    * [Build FFmepg with VPE plugin](#Build-FFmepg-with-VPE-plugin)
    * [Run FFmpeg](#Run-FFmpeg)
* [2.Plugin Parameters](#2.Plugin-Parameters)
    * [Device](#Device)
    * [Decoder](#Decoder)
    * [H264 HEVC Encoder](#H264-HEVC-Encoder)
    * [VP9 Encoder](#VP9-Encoder)
    * [Spliter](#Spliter)
    * [PP](#PP)
* [3.FFmpeg Command Line Examples](#3.FFmpeg-Command-Line-Examples)
    * [Transcoding](#Transcoding)
    * [Decoding Only](#Decoding-Only)
    * [Encoding Only](#Encoding-Only)
    * [Transcoding with parameters](#Transcoding-with-parameters)
    * [Downscaling First Pass](#Downscaling-First-Pass)
* [4.Online Typical Use Case](#5.Online-Typical-Use-Case)
    * [Encoding h264 hevc with low latency](#Encoding-h264-hevc-with-low-latency)
    * [Camera low latency streaming with dynamic bitrate fps resolution change](#Camera-low-latency-streaming-with-dynamic-bitrate-fps-resolution-change)
    * [RTMP Transcoding](#RTMP-Transcoding)
    * [4K source multiple outputs streaming](#4K-source-multiple-outputs-streaming)
    * [HDR10 Encoding](#HDR10-Encoding)
    * [Best performance and latency](#Best-encoding-performance-and-latency)
    * [Best transcoding performance latency](#Best-transcoding-performance-latency)
    * [Best encoding quality](#Best-encoding-quality)
    * [Best transcoding quality](#Best-transcoding-quality)
    * [Balance encoding quality and performance](#Balance-encoding-quality-and-performance)
    * [Balance transcoding quality and performance](#Balance-transcoding-quality-and-performance)
    * [Multiple hardware device in one command](#Multiple-hardware-device-in-one-command)

* [5.Use VPE in K8s](#4.Use-VPE-in-K8S)

# 1.Introducing

This project is VPE plugin development trunk, it keep synced with FFmpeg master branch.

## 1.1 Plugin Summary

| Plugin | Type| Comments| Capbilities|
|---------------|---------|---------------------------------------------------------------------------------------------------------------|----------------------------------------------------|
| vpe | device| FFmpeg device which be used by parameter “-init_hw_device”, for example “ffmpeg vpe=dev0:/dev/transcoder0” ||
| h264enc_vpe| encoder | H264 hardware encoder interface | Maximum 4K 60Hz, High 10 Profile, levels 1 - 5.2 |
| hevcenc_vpe| encoder | HEVC hardware encoder interface | Maximum 4K 60Hz, Main 10 Profile, levels 5.1|
| vp9enc_vpe | encoder | VP9 hardware encoder interface| Maximum 4K 60Hz Profile 2 (10-bit)|
| h264_vpe | decoder | H264 hardware decoder interface | Maximum 4K 60Hz, High 10 Profile, levels 1 - 5.2 |
| hevc_vpe | decoder | HEVC hardware decoder interface | Maximum 4K 60Hz, Main 10 Profile, levels 5.1|
| vp9_vpe| decoder | VP9 hardware decoder interface| Maximum 4K 60Hz Profile 2 (10-bit)|
| vpe_pp | filter| Post Processing Filter| Upload raw data to hardware encoders, and doing the video downscaling, <br>Suppported raw data format:<br> YUV420P <br> YUV422P<br> NV12<br> NV21<br> YUV420P10LE<br>YUV420P10BE<br>YUV422P10LE<br>YUV422P10BE<br>P010LE<br>P010BE<br>YUV444P<br>RGB24<br>BGR24<br>ARGB<br>RGBA<br>ABGR<br>BGRA<br>|
| spliter_vpe| filter| Spliter | Spliter input video to Maximum 4 paths |

Below is the diagram:

![VPE Plugin Diagram](https://raw.githubusercontent.com/VeriSilicon/vpe/vs_develop/doc/vpe.svg)
## 1.2 Install VPE
1. Install VPE supported hardware like Solios-X to your computer;
2. Download VPE driver:
    ```bash
    # git clone https://github.com/VeriSilicon/vpe.git
    ```
3. Config tollchain:
    If you are doing cross-compiling, please run ./configure to config tollchain related setting.

    ```bash

    ./configure --arch=aarch64 --cross=aarch64-linux-gnu- --sysroot=toolchain/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/aarch64-linux-gnu/libc --kernel=/work/imx8mmevk-poky-linux/linux-imx/4.19.35-r0/build

    arch=aarch64
    cross=aarch64-linux-gnu-
    sysroot=/toolchain/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/aarch64-linux-gnu/libc
    kernel=/work/imx8mmevk-poky-linux/linux-imx/4.19.35-r0/build
    debug=n
    Create VPE build config file successfully!

    ## lunch the config file
    . config.mk
    ```
4. Build VPE:
    then you can build VPE
    ```bash
    # make
    ```

5. Build and install driver:
    * The kernel driver need to be build and install
    ```bash
    # sudo make drivers
    ```

6. Install VPE:
    * All of the VPE output files will be copied to vpe/package;
    * For non-cross build, the VPE lib will be installed to "$installpath" folder which was specified by "./configure --installpath="; if "$installpath" is not set, then the VPE will be installed to your system folder;
    * For cross build, the VPE lib will be installed to "$installpath" folder which was specified by "./configure --installpath="; if $installpath is not set, then the VPE will not be installed.

    ```bash
    # sudo make install
    ```

7. Get FFmpeg source code:
    ```bash
    # git clone https://github.com/VeriSilicon/ffmpeg.git
    ```

8. Build and install FFmpeg
    ```bash
    # cd ffmpeg
    # sudo ldconfig ## only need after VPE first installation
    # sudo depmod ## only need after VPE first installation
    # ./configure --pkg-config=true --enable-vpe --extra-ldflags="-L/usr/lib/vpe" --extra-libs="-lvpi"
    # make -j8
    # sudo make install
    ```

## 1.5 Run FFmpeg
```bash
ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -transcode 1 \
-i ${INPUT_FILE_H264} -c:v h264enc_vpe out0.h264
```

Get more examples:[Transcoding](#Transcoding), [Decoding Only](#Decoding-Only), [Encoding Only](#Encoding-Only), [Transcoding with parameters](#Transcoding-with-parameters), [Downscaling First Pass](#Downscaling-First-Pass)

# 2.Plugin Parameters
## Device

| Option | Sub Option | Type | Description| Range| Default Value |
|------------|------------|--------|--------------------------------------------|----------------------|---------------|
| priority ||string | Priority of codec, live priority is higher than vod | live,vod | vod |
| vpeloglevel ||int| Set the VPE log level. <br>0:disable<br>3:error<br>4:warning <br>5:information<br>6:debug<br>7:verbose | 0-9 |

Example：
    ```bash
    ffmpeg -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=live,vpeloglevel=0
    ```
## Decoder
| Option| Sub Option | Type | Description| Range | Default Value|
|---------------|------------|--------|-----------------------------------------------------------------------------|----------------------|------------------|
| -low_res||string | Set output streams number and set the downscaling size for each stream.<br><br>1. The suppported minimal window for H264/HEVC is [128, 98], for VP9 is [66, 66] <br><br>2. The target windows width and height should always equal or less than source video width and heigh | H264/HEVC: W>=128 H>=98<br><br>VP9: <br>W>=66 H>=66 |
| -dev ||string | Set device name||/dev/transcoder0 |
| -transcode ||int | Whether need doing transcoding, for decoder and encoder only case, this opition is not required.| 0,1|0|

Example：
Below example will do hevc->h264 transcoding and output 2 streams: one is orignal resolution h264 stream, the second one is 640x360 h264 stream.
```bash
ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v h264enc_vpe out0.h264 -map '[out1]' -c:v h264enc_vpe out1.h264
```

#### "low_res" formats

For decoder supports multiple formats, supporse orignal stream resolution is W x H:

1. Fixed resolution. Below example will output 4 streams: [W x H], [1920x1080], [1280x720], [640x360]:
> -low_res 4:(1920x1080)(1280x720)(640x360)

2. Relative resolution. Below example will output 4 streams: [W x H], [960x540], [480x270], [640x360]
> -low_res 4:(d2)(d4)(d8)

3. Equal proportion: H or H will follow the scaling ratio.
 Below example will output 4 streams: [W x H], [(W x (1080/H)) x 1080], [(W x (720/H) x 720], [(W x (360/H) x 360]
> -low_res 4:(-1x1080)(-1x720)(-1x360)

 Below example will output 4 streams: [W x H], [(1920 x (H*1920/W)], [1280 x (H*1280/W)], [640 x (H*640/W)]

> -low_res 4:(1920x-1)(1280x-1)(640x-1)

4. Cropping and scaling: Two sets are required for each stream: scropping setting and scaling setting. IN below example, the first scropping window is (10,10,3840x2160), the scaling target resolution is 1920x1080
> -low_res '2:(10,10,3840,2160,1920x1080)'

5. <font color="#0000dd">Special use case of "low_res"</font>
  Set <font color="#0000dd">"-low_res 1:(d2)"</font> in decoder or set <font color="#0000dd">"low_res=(d2)"</font> in pp can reduce the medium/slow/superslow transcoding performance with sligh quality drop:
  * H264 encoding: 50%
  * HEVC encoding: 20%
  * Video quality drop < 1%
command line example please refer [Downscaling First Pass](#Downscaling-First-Pass),and [Balance quality & performance (encoding)](#Balance-quality-&-performance-(encoding))


6. "low_res" is also for vpe_pp filter, the only difference is only the streams numbers is not required in vpe_pp filer, for example:
> -filter_complex 'vpe_pp=(1920x1080)(1280x720)(640x360)'

> -filter_complex 'vpe_pp=low_res=(d2)'


## H264 HEVC Encoder

| Option | Sub Option| Type | Description|
|--------|-----------|------|------------|
| -b:v| | int| The target bit rate in bits per second (bps) when the rate control is enabled. The rate control is considered enabled when pic_rc, or hrd is enabled. When HRD is enabled the bitrate must be within the limits set for the encoder level (refer to Table 1). <br>Range: [10000...levelMax]<br>Default: 1000000|
| -r (for input)| | int| Input picture rate numerator. <br>Range:  [1...1048575] <br>Default: 30 |
||| int| Input picture rate denominator. <br>Range: [1...1048575] <br>Default: 1|
| -r (for output) | | int| Output picture rate numerator.<br>Range: [1...1048575] <br>Default: same with input |
||| int| Output picture rate denominator.<br>Range: [1...1048575] <br>Default: same with input |
| -profile:v| | string| The HEVC/H.264 profile of the generated stream. <br>Range: <br>HEVC = main/still/main10 <br>H264 = base/main/high/high10<br>Default: HEVC=main, H264=main |
| -level| | string| The HEVC/H.264 level of the generated stream. <br>Range: <br>HEVC=[1.0/2.0/2.1/3.0/3.1/4.0/4.1/5.0/5.1] <br>H264=[1/1b/1.1/1.2/1.3/2/2.1//2.2/3/3.1/3.2/4/4.1/4.2/5/5.1/5.2]<br>Default: HEVC=5.1, H264=5.2|
| -crf| | int| VCE Constant rate factor mode. Can only works with two pass mode - lookahead turned on. <br>Range: [-1...51]<br>Default: -1|
| -force_idr| | int| transcoding case only. If this flag is set, then the I frames position will keep same with input stream. <br>Range: 0 = disable; 1 = enable<br>Default: 0|
| -preset | | string | Encoding preset.<br>Range:superfast/fast/medium/slow/superslow<br>Default: fast|
| -enc_params | | | |
||low_delay | int| Software level latency control flag. if it's set to 1, then VPE will works in single thread mode, in this mode the overall devely is around 3ms. Otherwise VPE works in multi-threads mode, in this case delay can be >30ms. 1 means enable low lantency mode.<br>Range: 0 = disable low delay mode; 1 = enable low delay mode<br>Default: 0 |
||intra_pic_rate| int| I frame interval in frames. If intra_pic_rate and force_idr are not specified, then there will be only one I frame be encoded. <br>Range: if lookahead_depth=0: [0...INT_MAX]; if lookahead_depth>0: [2...INT_MAX]<br>Default: 0|
||bitrate_window | int| Bitrate window length in frames.<br>Range: [1...300]<br>Default: if intra_pic_rate is not set=150; otherwise=intra_pic_rate|
||intra_qp_delta| int| Delta value added to the Intra frame QP. Min/Max range checking still applies. Can be used to lower the Intra picture encoded size (higher QP) or to increase Intra quality relative to the Inter pictures (lower QP) to get rid of intra flashing. <br>Range: [-51...51] <br>Default: -5|
||qp_hdr | int| The initial Quantization Parameter (QP) used by the encoder. If the rate control is enabled then this value is used only at the beginning of the encoding process. When the rate control is disabled then this QP value is used all the time. -1 lets RC calculate initial QP. Not recommended to be set lower than 10.<br>Range: [0...51] <br>Default: -1 |
||qp_min_I| int| Minimum frame header QP for I slices.<br>Range: [0...51]<br>Default: 0|
||qp_max_I| int| Maximum frame header QP for I slices.<br>Range: [0...51]<br>Default: 51 |
||qp_min | int| Minimum frame header QP for any slices.<br>Range: [0...51]<br>Default: 0|
||qp_max | int| Maximum frame header QP for any slices.<br>Range: [0...51]<br>Default: 51 |
||fixed_intra_qp| int| Use this value for all Intra picture quantization. Value 0 disables the feature. Min/Max range checking still applies. intraQpDelta does not apply when fixedIntraQp is in use.<br>Range: [0...51]<br>Default: 0|
||tier| int| Encoder tier. <br>Range: 0 = main tier; 1 = high tier<br>Default: 0|
||byte_stream| int| Stream type. <br>Range: 0 = NAL units; 1 = byte stream<br>Default: 1|
||video_range| int| Video signal sample range value in Hevc stream. <br>Range:  0 = Y in [16...235] and Cb/Cr in [16...240] ; 1 = range in [0...255]<br>Default: 1|
||sei | int| Enables insertion of picture timing and buffering period SEI messages into the stream in the beginning of every encoded frame. <br>Range: 0 = SEI disable; 1 = SEI enable<br>Default: 0|
||cabac | int|  H.264 entropy coding mode.<br>Range: 0 = cavlc; 1 = cabac<br>Default: 1|
||slice_size | int| Sets the size of a slice in CTB rows. A zero value disables the use of slices. This parameter can be updated during the encoding process, between any picture encoding. <br>Range: [0...height/ctu_size]<br>Default: 0|
||pic_rc | int| Enables picture level rate control to adjust QP between frames. This should be enabled if target bit rate is set.<br>Range: 0 = OFF<br>1 = ON<br>Default: 0|
||pic_rc_config | string| Picture rate config file, in plain text file mode, include fps/resolution/bitrate per frame control. <br><br>config file format example: <br>bsp:(100000)<br>res:(1280)(720)<br>fps:(25)(1).<br><br> **Note: fps on-the-fly change is only available when vpe_pp was connected.** <br><br>[sample config file](https://raw.githubusercontent.com/VeriSilicon/vpe/vs_develop/doc/rc_example.cfg)| bps:[10000...60000000]<br><br>res:[width and height lower than source]<br><br>fps:[lower than source fps] | NA |
||ctb_rc | int| Enables CTB level rate control to adjust QP between CTBs within frame. <br>Range: 0 = Disable CTB level rate control, 1 = Enables CTB level rate control<br>Default: 0|
||pic_qp_delta_range | int| Picture level QP Range delta. format: [Min_Max].<br>Range: Min = [-1...-10]; Max = [1...10] <br>Default: Min=-2, Max=3 |
||hrd_conformance| int| Enables the use of Hypothetical Reference Decoder (HRD) model to restrict the instantaneous bitrate. Enabling the HRD will automatically enable the picture rate control. <br>Range: 0 = disable HRD; 1 = enable HRD<br>Default: 0|
||cpb_size | int| Size in bits of the Coded Picture Buffer (CPB) used by the HRD model. When HRD is enabled an encoded frame can’t be bigger than CPB. By default the encoder will use the maximum allowed size for the initialized encoder level (refer to Table 1).  Setting this value to 0 will always restore the default size. <br>Range: [0, MaxCPB]<br>Default: 1000000|
||gop_size | int| This parameter controls P frame interval.<br>Range: 0 = adaptive size; 1-8 = fixed size<br>Default: 0|
||gop_lowdelay | int| Enable default lowDelay GOP configuration, only valid for GOP size <= 4.<br>Range: [0...1]<br>Default: 0|
||chroma_qp_offset| int| Chroma QP offset.<br>Range: [-12...12]<br>Default: 0|
||vbr | int| Enable variable Bit Rate Control by qpMin.<br>Range: 0 = OFF; 1 = ON<br>Default: 0|
||user_data| string | User data to the encoded stream. The user data will be written in the stream as a Supplemental Enhancement Information (SEI) message connected to all the following encoded frames.||NULL|
||intra_area | int| Specifies a rectangular area of a Coding Tree Block (CTB) to be forced as intra coded. All CTBs inside the area and including the coordinates will be intra coded. format: left_top_right_bottom. <br>Range: <=video window <br>Default: -1|
||ipcm1_area | int| CTB coordinates specifying rectangular area of CTBs to force encoding in IPCM mode. format: left_top_right_bottom<br>Range: <=video window<br>Default: -1|
||ipcm2_area | int| CTB coordinates specifying rectangular area of CTBs to force encoding in IPCM mode. format: left_top_right_bottom<br>Range: <=video window<br>Default: -1|
||const_chroma | int| Enable setting chroma a constant pixel value. <br>Range: [0...1]<br>Default: 0|
||const_cb | int| The constant pixel value for Cb.<br>Range: 8 bit=[0...255]; 10 bit=[0...1023] <br>Default: 8 bit=128, 10 bit=512 |
||const_cr | int| The constant pixel value for Cr.<br>Range: 8 bit=[0...255]; 10 bit=[0...1023] <br>Default: 8 bit=128, 10 bit=512 |
||rdo_level| int| Control RDO hardware runtime effort level; balance between quality and throughput performance. <br>Range: 1 = RDO run 1x candidates; 2 = RDO run 2x candidates; 3 = RDO run 3x candidates <br>Default: 1|
||ssim| int| Enable SSIM Calculation.<br>Range: 0 = Disable; 1 = Enable<br>Default: 1|
||vui_timing_info | int| Write VUI timing info in SPS.<br>Range: 0 = Disable; 1 = Enable<br>Default: 1|
||lookahead_depth| int| Number of lookahead frames. 0 means disable two pass, to enable two pass, it can be set to 4-40.<br>Range: 0; [4...40]<br>Default: 0|
||force8bit| int| If this flag is set then the input streams will be force to 8bit.<br>Range: [0...1]<br>Default: 0|
||mastering_display_en|int|HDR10 setting. Mastering display colour stream.<br>Range: [0...1]<br>Default: 0|
||display_pri_x0 | int |HDR10 setting. Green display primary x. <br>Range: [0...50000]<br>Default: 0|
||display_pri_y0 | int |HDR10 setting. Green display primary y.<br>Range: [0...50000]<br>Default: 0|
||display_pri_x1 | int |HDR10 setting. Blue display primary x.<br>Range: [0...50000]<br>Default: 0|
||display_pri_y1 | int |HDR10 setting. Blue display primary y.<br>Range: [0...50000]<br>Default: 0|
||display_pri_x2 | int |HDR10 setting. Red display primary x.<br>Range: [0...50000]<br>Default: 0|
||display_pri_y2 | int |HDR10 setting. Red display primary y.<br>Range: [0...50000]<br>Default: 0|
||white_point_x | int |HDR10 setting. White point x.<br>Range: [0...50000]<br>Default: 0|
||white_point_y | int |HDR10 setting. White point y.<br>Range: [0...50000]<br>Default: 0|
||min_luminance | int |HDR10 setting. Min display mastering luminance.<br>Range: [0...2^32 - 1]<br>Default: 0|
||max_luminance | int |HDR10 setting. Max display mastering luminance.<br>Range: [0...2^32 - 1]<br>Default: 0|
||light_level_en | int |HDR10 setting. Content light level enable.<br>Range: [0...1]<br>Default: 0|
||max_content_light_level |int |HDR10 setting. Max content light leve.<br>Range: [0...2^16 - 1]<br>Default: 0|
||max_pic_average_light_level | int |HDR10 setting. Max pic average light level.<br>Range: [0... 2^16 - 1]<br>Default: 0|

Example:
> -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60"

## VP9 Encoder

| Option | Sub Option| Type | Description|
|--------|-----------|------|------------|
| -b:v | | int |Target bitrate for rate control. <br>Range: [10000...60000000]<br>Default: 1000000|
| -r (for input) | | int| Input picture rate numerator.<br>Range: [1...1048575]<br>Default: 30 |
| | | int| Input picture rate denominator.<br>Range: [1...1048575]<br>Default: 1|
| -r (for output)| | int| Output picture rate numerator.<br>Range: [1...1048575]<br>Default: inputRateNumer |
| | | int| Output picture rate denominator.<br>Range: [1...1048575] <br>Default: inputRateDenom |
| -effort | | int| Encoder effort level.<br>Range: [0...5]<br> 0 - fastest <br>5 - best quality <br>Default: 0|
| -lag_in_frames||int| Number of frames to lag. Up to 25.<br>Range: [0...25]<br>Default: 7|
| -passes | | int| Number of passes.<br>Range: [1...2]<br>Default: 1|
| -profile:v | | int| Encoder profile.<br>Range:  [0...3] <br>Default: 0|
| -preset | | string | Encoding preset.<br>Range: superfast/fast/medium/slow/superslow<br>Default: fast |
| -enc_params
| |low_delay | int| Software level latency control flag. if it's set to 1, then VPE will works in single thread mode, in this mode the overall devely is around 3ms. Otherwise VPE works in multi-threads mode, in this case delay can be >30ms. <br>Range: 0 = disable low delay mode; 1 = enable low delay mode<br>Default: 0 |
| | effort| int| Encoder effort level.<br>Range: [0...5]<br> 0 - fastest <br>5 - best quality<br>Default: 0|
| | lag_in_frames | int| Number of frames to lag. Up to 25.<br>Range: [0...25]<br>Default: 7|
| | passes| int| Number of passes.<br>Range: [1...2]<br>Default: 1|
| | intra_pic_rate| int| Intra picture rate in frames.<br>Range: [0...INT_MAX]<br>Default: 0|
| | bitrate_window | int| Bitrate window length in frames.<br>Range: [1...300]<br>if intra_pic_rate is not set=150; otherwise=intra_pic_rate|
| | qp_hdr | int| Initial QP used for the first frame.<br>Range: [0...255]<br>Default: -1|
| | qp_min | int| Minimum frame header QP.<br>Range: [0...255]<br>Default: 10 |
| | qp_max | int| Maximum frame header QP.<br>Range: [0...255]<br>Default: 255|
| | fixed_intra_qp| int| Fixed Intra QP, 0 = disabled.<br>Range: [0...255] <br>Default: 0|
| | pic_rc | int| Picture rate control enable.<br>Range: 0 - OFF; 1 - ON<br>Default: 1|
| | mcomp_filter_type | int| Interpolation filter mode.<br>Range: [0...4] <br>Default: 4|
| | force8bit | int| Force to output 8bit stream.<br>Range: 0 = not force input as 8 bit; 1 = force input as 8 bit <br>Default: 0|
| | ref_frame_scheme| int| Reference frame update scheme.<br>Range: [0...4]<br>Default: 4|
| | filte_level | int| Filter strength level for deblocking.<br>Range: [0...64]<br>Default: 64 |
| | filter_sharpness | int| Filter sharpness level for deblocking.<br>Range: [0...8], 8=auto<br>Default: 8|

Example:
> -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60"

# Preset detail parameters:
| Level | format | Parameters setting| comment |
|-----------|--------|-------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------|
| superfast | h264 |  <br>intra_qp_delta=-2 <br>qp_hdr=-1 <br>pic_rc=1 <br>ctb_rc=0 <br>gop_size=1 | |
| | hevc |  <br>intra_qp_delta=-2 <br>qp_hdr=-1 <br>pic_rc=1 <br>ctb_rc=0 <br>gop_size=1 <br>rdo_level=1| |
| | vp9|  <br>qp_hdr=-1 <br>pic_rc=1 <br>effort=0 <br>mcomp_filter_type=4 <br>ref_frame_scheme=0 <br>lag_in_frames=0| |
| fast| h264 |  <br>intra_qp_delta=-2 <br>qp_hdr=-1 <br>pic_rc=1 <br>ctb_rc=0 <br>gop_size=4 ||
| | hevc |  <br>intra_qp_delta=-2 <br>qp_hdr=-1 <br>pic_rc=1 <br>ctb_rc=0 <br>gop_size=4 <br>rdo_level=1| |
| | vp9|  <br>qp_hdr=-1 <br>pic_rc=1 <br>effort=0 <br>mcomp_filter_type=4 <br>ref_frame_scheme=4 <br>lag_in_frames=7| |
| medium| h264 |  <br>intra_qp_delta=-2 <br>qp_hdr=-1 <br>pic_rc=1 <br>ctb_rc=0 <br>gop_size=4 <br>lookahead_depth=20 | |
| | hevc |  <br>intra_qp_delta=-2 <br>qp_hdr=-1 <br>pic_rc=1 <br>ctb_rc=0 <br>gop_size=4 <br>rdo_level=1 <br>lookahead_depth=20| |
| | vp9|  <br>qp_hdr=-1 <br>pic_rc=1 <br>effort=0 <br>mcomp_filter_type=4 <br>ref_frame_scheme=4 <br>passes=2 <br>lag_in_frames=12 | |
| slow| h264 |  <br>intra_qp_delta=-2 <br>qp_hdr=-1 <br>pic_rc=1 <br>ctb_rc=0 <br>gop_size=0 <br>lookahead_depth=30 | adaptive GOP size |
| | hevc |  <br>intra_qp_delta=-2 <br>qp_hdr=-1 <br>pic_rc=1 <br>ctb_rc=0 <br>gop_size=0 <br>rdo_level=2 <br>lookahead_depth=30| adaptive GOP size |
| | vp9|  <br>qp_hdr=-1 <br>pic_rc=1 <br>effort=1 <br>mcomp_filter_type=4 <br>ref_frame_scheme=4 <br>passes=2 <br>lag_in_frames=25 ||
| superslow | h264 |  <br>intra_qp_delta=-2 <br>qp_hdr=-1 <br>pic_rc=1 <br>ctb_rc=0 <br>gop_size=0 <br>lookahead_depth=40 ||
| | hevc |  <br>intra_qp_delta=-2 <br>qp-hdr=-1 <br>pic_rc=1 <br>ctb_rc=0 <br>gop_size=0 <br>rdo_level=3 <br>lookahead_depth=40||
| | vp9|  <br>qp_hdr=-1 <br>pic_rc=1 <br>effort=2 <br>mcomp_filter_type=4 <br>ref_frame_scheme=4 <br>passes=2 <br>lag_in_frames=25 ||

## Spliter
| Option| Sub Option | Type | Description | Range | Default Value |
|---------|------------|------|-------------------------|----------------------|---------------|
| outputs ||int| Set number of outputs. | [1...4] | 1 |


## PP
| Option | Sub Option | Type | Description| Range | Default Value |
|------------|------------|--------|------------------------------------------------------|----------------------|---------------|
| outputs||int| Set number of outputs.| [1...4] | 1 |
| force10bit ||int| force output 10bit format| [0...1] | 0 |
| low_res ||string | Set output number and resize config for each channel ||null|

Note: low_res is almost same which defined in [decoder filter](#Decoder), the only difference is the streams numbers is not required in vpe_pp filer, for example:
> low_res=(1920x1080)(1280x720)(640x360).

#

# 3.FFmpeg Command Line Examples

## Transcoding

One Output Diagram:

![](https://raw.githubusercontent.com/VeriSilicon/vpe/vs_develop/doc/transcoding_one_output.svg)

Four Output Diagram:

![](https://raw.githubusercontent.com/VeriSilicon/vpe/vs_develop/doc/transcoding_four_outputs.svg)

| Case | Target | Source | Output | 2 Pass | Command Line Example|
|------|--------|--------|------------|----------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format | Numbers| Encoding | |
| 1| h264 | h264 | 1| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -i ${INPUT_FILE_H264} -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264|
| 2| h264 | h264 | 2| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset fast -b:v 5000000 out1.h264|
| 3| h264 | hevc | 3| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset fast -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset fast -b:v 3000000 out2.h264 |
| 4| h264 | hevc | 4| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset fast -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset fast -b:v 3000000 out2.h264 -map '[out3]' -c:v h264enc_vpe -preset fast -b:v 500000 out3.h264|
| 5| h264 | vp9| 1| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -i ${INPUT_FILE_VP9} -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264 |
| 6| h264 | vp9| 2| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset fast -b:v 5000000 out1.h264|
| 7| hevc | h264 | 3| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset fast -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset fast -b:v 3000000 out2.hevc |
| 8| hevc | h264 | 4| N|ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset fast -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset fast -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset fast -b:v 500000 out3.hevc |
| 9| hevc | hevc | 1| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -i ${INPUT_FILE_HEVC} -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc|
| 10 | hevc | hevc | 2| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset fast -b:v 5000000 out1.hevc|
| 11 | hevc | vp9| 3| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset fast -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset fast -b:v 3000000 out2.hevc |
| 12 | hevc | vp9| 4| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset fast -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset fast -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset fast -b:v 500000 out3.hevc|
| 13 | vp9| h264 | 1| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -i ${INPUT_FILE_H264} -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf|
| 14 |||2| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "2:(360x640)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset fast -b:v 5000000 out1.ivf|
| 15 | vp9| hevc | 3| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset fast -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset fast -b:v 3000000 out2.ivf |
| 16 | vp9| hevc | 4| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset fast -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset fast -b:v 3000000 out2.ivf -map '[out3]' -c:v vp9enc_vpe -preset fast -b:v 500000 out3.ivf|
| 17 | vp9| vp9| 1| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -i ${INPUT_FILE_VP9} -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf|
| 18 | vp9| vp9| 2| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset fast -b:v 5000000 out1.ivf|
| 19 | h264 | h264 | 1| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -i ${INPUT_FILE_H264} -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264|
| 20 | h264 | h264 | 2| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset medium -b:v 5000000 out1.h264|
| 21 | h264 | hevc | 3| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset medium -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset medium -b:v 3000000 out2.h264 |
| 22 | h264 | hevc | 4| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset medium -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset medium -b:v 3000000 out2.h264 -map '[out3]' -c:v h264enc_vpe -preset medium -b:v 500000 out3.h264|
| 23 | h264 | vp9| 1| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -i ${INPUT_FILE_VP9} -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264|
| 24 | h264 | vp9| 2| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset medium -b:v 5000000 out1.h264|
| 25 | hevc | h264 | 3| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset medium -b:v 3000000 out2.hevc |
| 26 | hevc | h264 | 4| Y|ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset medium -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset medium -b:v 500000 out3.hevc |
| 27 | hevc | hevc | 1| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -i ${INPUT_FILE_HEVC} -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc|
| 28 | hevc | hevc | 2| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc|
| 29 | hevc | vp9| 3| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset medium -b:v 3000000 out2.hevc |
| 30 | hevc | vp9| 4| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset medium -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset medium -b:v 500000 out3.hevc|
| 31 | vp9| h264 | 1| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -i ${INPUT_FILE_H264} -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf|
| 32 | vp9| h264 | 2| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "2:(360x640)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset medium -b:v 5000000 out1.ivf|
| 33 | vp9| hevc | 3| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset medium -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset medium -b:v 3000000 out2.ivf |
| 34 | vp9| hevc | 4| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset medium -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset medium -b:v 3000000 out2.ivf -map '[out3]' -c:v vp9enc_vpe -preset medium -b:v 500000 out3.ivf|
| 35 | vp9| vp9| 1| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -i ${INPUT_FILE_VP9} -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf|
| 36 | vp9| vp9| 2| Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset medium -b:v 5000000 out1.ivf|
| 37 | VP9| HEVC | 1| N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "2:(d2)" -r 30 -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -f null /dev/null -map '[out1]' -c:v vp9enc_vpe -preset medium -b:v 5000000 out1.ivf

## Decoding Only

One Output Diagram:

![](https://raw.githubusercontent.com/VeriSilicon/vpe/vs_develop/doc/decoding_one_output.svg)

Four Output Diagram:

![](https://raw.githubusercontent.com/VeriSilicon/vpe/vs_develop/doc/decoding_four_output.svg)

| Case | Target | Source | Output| 2 Pass | Command Line Example |
|------|--------|--------|---------|----------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format | Numbers | Encoding ||
| 38 | nv12 | h264 | 1 | NA | ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -i ${INPUT_FILE_H264} -filter_complex 'hwdownload,format=nv12' out0.yuv  |
| 39 | nv12 | h264 | 4 | NA | ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=4[1][2][3][4],[1]hwdownload,format=nv12[a],[2]hwdownload,format=nv12[b],[3]hwdownload,format=nv12[c],[4]hwdownload,format=nv12[d]' -map '[a]' out0.yuv -map '[b]' out1.yuv -map '[c]' out2.yuv -map '[d]' out3.yuv |
| 40 | nv12 | hevc | 4 | NA | ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=4[1][2][3][4],[1]hwdownload,format=nv12[a],[2]hwdownload,format=nv12[b],[3]hwdownload,format=nv12[c],[4]hwdownload,format=nv12[d]' -map '[a]' out0.yuv -map '[b]' out1.yuv -map '[c]' out2.yuv -map '[d]' out3.yuv |
| 41 | nv12 | vp9| 4 | NA | ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=4[1][2][3][4],[1]hwdownload,format=nv12[a],[2]hwdownload,format=nv12[b],[3]hwdownload,format=nv12[c],[4]hwdownload,format=nv12[d]' -map '[a]' out0.yuv -map '[b]' out1.yuv -map '[c]' out2.yuv -map '[d]' out3.yuv |

## Encoding Only
One Output Diagram:

![](https://raw.githubusercontent.com/VeriSilicon/vpe/vs_develop/doc/encoding_one_output.svg)

Four Output Diagram:

![](https://raw.githubusercontent.com/VeriSilicon/vpe/vs_develop/doc/encoding_four_outputs.svg)
| Case | Target | Source| Output| 2 Pass | Command Line Example|
|------|--------|---------|---------|----------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format| Numbers | Encoding | |
| 42 | h264 | yuv420p | 1 | N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -s 1920x1080 -pix_fmt yuv420p -i ${INPUT_FILE_RAW_420P} -filter_complex 'vpe_pp' -c:v h264enc_vpe -preset superfast -b:v 10000000 out0.h264  |
| 43 | h264 | yuv420p | 4 | N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -s 1920x1080 -pix_fmt yuv420p -i ${INPUT_FILE_RAW_420P} -filter_complex 'vpe_pp=outputs=4:low_res=(1920x1080)(1280x720)(640x360),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v h264enc_vpe -preset superfast -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset superfast -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset superfast -b:v 3000000 out2.h264 -map '[out3]' -c:v h264enc_vpe -preset superfast -b:v 500000 out3.h264 |
| 44 | hevc | yuv420p | 4 | N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -s 1920x1080 -pix_fmt yuv420p -i ${INPUT_FILE_RAW_420P} -filter_complex 'vpe_pp=outputs=4:low_res=(1920x1080)(1280x720)(640x360),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset superfast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset superfast -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset superfast -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset superfast -b:v 500000 out3.hevc|
| 45 | vp9| yuv420p | 4 | N| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -r 30 -s 1920x1080 -pix_fmt yuv420p -i ${INPUT_FILE_RAW_420P} -filter_complex 'vpe_pp=outputs=4:low_res=(1920x1080)(1280x720)(640x360),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v vp9enc_vpe -preset superfast -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset superfast -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset superfast -b:v 3000000 out2.ivf -map '[out3]' -c:v vp9enc_vpe -preset superfast -b:v 500000 out3.ivf |
| 46 | h264 | rgb24 | 4 | Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -r 30 -s 1920x1080 -pix_fmt rgb24 -i ${INPUT_FILE_RAW_RGB24} -filter_complex 'vpe_pp=outputs=4:low_res=(d2)(d4)(d8),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset medium -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset medium -b:v 3000000 out2.h264 -map '[out3]' -c:v h264enc_vpe -preset medium -b:v 500000 out3.h264|
| 47 | hevc | abgr| 4 | Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -r 30 -s 1920x1080 -pix_fmt abgr -i ${INPUT_FILE_RAW_ABGR} -filter_complex 'vpe_pp=outputs=4:force10bit=1:low_res=(d2)(d4)(d8),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset medium -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset medium -b:v 500000 out3.hevc |
| 48 | vp9| rgba| 4 | Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -r 30 -s 1920x1080 -pix_fmt rgba -i ${INPUT_FILE_RAW_RGBA} -filter_complex 'vpe_pp=outputs=4:low_res=(-1x1080)(-1x674)(-1x336),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v vp9enc_vpe -preset slow -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset slow -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset slow -b:v 3000000 out2.ivf -map '[out3]' -c:v vp9enc_vpe -preset slow -b:v 500000 out3.ivf|

## Transcoding with parameters

| Case | Target | Source | Output| 2 Pass | Command Line Example|
|------|--------|--------|---------|----------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format | Numbers | Encoding | |
| 49 | h264 | h264 | 1 | Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -transcode 1 -i ${INPUT_FILE_H264} -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 10000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out0.h264  |
| 50 | h264 | h264 | 3 | Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 10000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out0.h264 -map '[out1]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 5000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out1.h264 -map '[out2]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 3000000 -enc_params "lookahead_depth=10:chroma_qp_offset='2':bitrate_window=180:intra_pic_rate=60" out2.h264 |
| 51 | h264 | argb | 4 | Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -s 1920x1080 -pix_fmt argb -i ${INPUT_FILE_RAW_ARGB} -filter_complex "vpe_pp=outputs=4:force10bit=1:low_res=(1920x1080)(1280x720)(640x360),spliter_vpe=outputs=4[out0][out1][out2][out3]" -map '[out0]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 10000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out0.h264 -map '[out1]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 5000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out1.h264 -map '[out2]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 3000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out2.h264 -map '[out3]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 500000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out3.h264 |
| 52 | vp9| h264 | 3 | Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v vp9enc_vpe -b:v 10000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out0.ivf -map '[out1]' -c:v vp9enc_vpe -b:v 5000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out1.ivf -map '[out2]' -c:v vp9enc_vpe -b:v 3000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out2.ivf |
| 53 | vp9| nv12 | 4 | Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -s 1920x1080 -pix_fmt nv12 -i ${INPUT_FILE_RAW_NV12} -filter_complex "vpe_pp=outputs=4:low_res=(1920x1080)(1280x720)(640x360),spliter_vpe=outputs=4[out0][out1][out2][out3]" -map '[out0]' -c:v vp9enc_vpe -r 60 -b:v 10000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out0.ivf -map '[out1]' -c:v vp9enc_vpe -b:v 5000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out1.ivf -map '[out2]' -c:v vp9enc_vpe -b:v 3000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out2.ivf -map '[out3]' -c:v vp9enc_vpe -b:v 500000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out3.ivf |

## Downscaling First Pass

The downscaled video instead of full size video will be used as the input of encoder to do second pass encoding; hence the performance can be improved.

Transcoding case:
<font color="#0000dd">-low_res "1:(d2)"</font> is the key parameter.

| Case | Target | Source | Output| 2 Pass | Command Line Example |
|------|--------|--------|---------|----------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format | Numbers | Encoding ||
| 55 | h264 | hevc | 1 | Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -c:v hevc_vpe -transcode 1 -low_res "1:(d2)" -i ${INPUT_FILE_HEVC} -c:v h264enc_vpe-preset medium -b:v 10000000 out0.h264 |


Encoding case:
-filter_complex 'vpe_pp<font color="#0000dd">=low_res=(d2)</font>' is the key parameter.

| Case | Target | Source | Output| 2 Pass | Command Line Example |
|------|--------|--------|---------|----------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format | Numbers | Encoding ||
| 54 | h264 | hevc | 1 | Y| ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0-s 1920x1080 -pix_fmt yuv420p -i ${INPUT_FILE_RAW_420P} -filter_complex 'vpe_pp=low_res=(d2)' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264

# 4.Online Typical Use Case
## Encoding h264 hevc with low latency
To enable low latency encoding, you need:
* Select "superfast" preset;
* Add "low_delay=1" in -enc_params;
```bash
ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 \
-s 1280x720 -pix_fmt nv12 -i ~/work/stream/out1280x720p_nv12.yuv \
-filter_complex 'vpe_pp' -c:v hevcenc_vpe -preset superfast \
-enc_params "low_delay=1" -b:v 10000000 out0.h264

Stream mapping:
  Stream #0:0 (rawvideo) -> vpe_pp
  vpe_pp -> Stream #0:0 (hevcenc_vpe)
Press [q] to stop, [?] for help
Output #0, h264, to 'out0.h264':
  Metadata:
    encoder         : Lavf58.64.100
    Stream #0:0: Video: hevc (hevcenc_vpe), vpe(progressive), 1280x720, q=2-31, 10000 kb/s, 25 fps, 25 tbn, 25 tbc
    Metadata:
      encoder         : Lavc58.112.101 hevcenc_vpe
frame=  102 fps= 37 q=-0.0 latency=  5ms Lsize=    2643kB time=00:00:03.76 bitrate=5759.1kbits/s speed=1.35x

```
You can get **"latency=  xms"** in the FFmpeg log, the x means the overall latency of the VPE encoding.

## Camera low latency streaming with dynamic bitrate fps resolution change
- Add **"pic_rc=1:pic_rc_config=rc.cfg"** into -enc_params to enable dynamic bitrate/fps change, then user can put target bitrate/fps in rc.cfg;
- Enable **"vpe_pp"** to enable dynamic resolution change function, then user can put target resolution in rc.cfg;
- Add **"low_delay=1"** into -enc_params, and select **"superfast"** preset to enable low latency mode;
```bash
sudo ./ffmpeg -y -report -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -vsync 0 \
-i /dev/video0 filter_complex "format=nv12,vpe_pp" \
-c:v hevcenc_vpe -preset superfast -b:v 2000000 \
-enc_params "low_delay=1:intra_pic_rate=5:pic_rc=1:pic_rc_config=rc.cfg" \
-f rtp_mpegts rtp://10.10.3.88:9999
```
Change bitrate on-the-fly:
```bash
echo "bps:100000" > rc.cfg
```
Change resolution on-the-fly:
```bash
echo "res:640x480" > rc.cfg
```
Change fps on-the-fly:
```bash
echo "fps:25/1" > rc.cfg
```
Change bitrate/resolution/fps on-the-fly:
```bash
echo "bps:100000" > rc.cfg
echo "res:640x480" >> rc.cfg
echo "fps:25/1" >> rc.cfg
```

Play the video in low-latency mode:
```bash
ffplay -probesize 32 -analyzeduration 0 -sync ext rtp://10.10.3.88:9999
```
## RTMP Transcoding
You need to setup RTMP streaming server first
###### Streaming:
```bash
ffmpeg -y -re -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe \
-transcode 1 -i http://ivi.bupt.edu.cn/hls/cctv5phd.m3u8 \
-c:v h264enc_vpe -preset medium -b:v 3000000 -level 5.1 -profile:v high -c:a copy
-f flv rtmp://10.10.3.88:1935/live/livestream
```
###### Play
```bash
ffplay rtmp://10.10.3.88:1935/live/livestream
```

## 4K source multiple outputs streaming
You need to setup RTMP streaming server first
###### Streaming:
```bash
ffmpeg -y -re -init_hw_device vpe=dev0:/dev/transcoder0 -c:v hevc_vpe -transcode 1 \
-low_res "4:(1920x1080)(1280x720)(640x360)" -i ~/work/stream/LaLaLand_cafe_4K.mkv \
-filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' \
-map '[out0]' -c:v h264enc_vpe -preset fast -b:v 10000000 -map 0:a -f flv rtmp://10.10.3.88:1935/live/10M  \
-map '[out1]' -c:v h264enc_vpe -preset fast -b:v 5000000 -map 0:a -f flv rtmp://10.10.3.88:1935/live/5M \
-map '[out2]' -c:v h264enc_vpe -preset fast -b:v 3000000 -map 0:a -f flv rtmp://10.10.3.88:1935/live/3M \
-map '[out3]' -c:v h264enc_vpe -preset fast -b:v 500000 -map 0:a -f flv rtmp://10.10.3.88:1935/live/500k
```
###### Play
```bash
ffplay rtmp://10.10.3.88:1935/live/10M
ffplay rtmp://10.10.3.88:1935/live/5M
ffplay rtmp://10.10.3.88:1935/live/3M
ffplay rtmp://10.10.3.88:1935/live/500k
```

## HDR10 Encoding

```bash
ffmpeg -y -report -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 \
-s 3840x2160 -pix_fmt yuv420p10le -i 4k10bithdr.yuv \
-filter_complex "hwupload" -c:v hevcenc_vpe  \
-color_primaries bt2020 -color_trc smpte2084 -colorspace bt2020nc \
-enc_params "mastering_display_en=1:display_pri_x0=13250:display_pri_y0=34500:\
display_pri_x1=7500:display_pri_y1=3000:display_pri_x2=34000:display_pri_y2=16000:\
white_point_x=15635:white_point_y=16450:min_luminance=0:max_luminance=10000000:\
light_level_en=1:max_content_light_level=1000:max_pic_average_light_level=640" 4khdr.hevc
```

## Best encoding performance and latency

encoder key parameters: -preset superfast
Sample:
```bash
ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -s 1920x1080 -pix_fmt yuv420p \
-i ${INPUT_FILE_RAW_420P} -filter_complex 'vpe_pp' -c:v h264enc_vpe -preset superfast \
-enc_params "low_delay=1" -b:v 10000000 out0.h264
```

## Best transcoding performance latency
Keywords:
<font color="#0000dd">-preset superfast -enc_params "low_delay=1" </font>

Sample:
```bash
ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -transcode 1 \
-i ${INPUT_FILE_H264} -c:v h264enc_vpe -preset superfast \
-enc_params "low_delay=1" -b:v 10000000 out0.h264

```

## Best encoding quality
Keywords:
<font color="#0000dd">-preset superslow
 -enc_params 'intra_pic_rate=60:gop_lowdelay=1'</font>

Sample:
```bash
ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -s 1920x1080 -pix_fmt yuv420p \
-i ${INPUT_FILE_RAW_420P} -filter_complex 'vpe_pp' -c:v h264enc_vpe -preset superslow \
-enc_params "intra_pic_rate=60:gop_lowdelay=1" -b:v 10000000 out0.h264
```

## Best transcoding quality
Keywords:
<font color="#0000dd">-enc_params 'intra_pic_rate=60:gop_lowdelay=1'
-preset superslow</font>

Sample:
```bash
ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -transcode 1 \
-i ${INPUT_FILE_H264} -c:v h264enc_vpe -preset -preset superslow \
-enc_params "intra_pic_rate=60:gop_lowdelay=1" -b:v 10000000 out0.h264
```

## Balance encoding quality and performance
Keywords:
<font color="#0000dd"> -filter_complex 'vpe_pp=low_res=(d2)'"
 -preset medium </font>

Sample:
```bash
ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -s 1920x1080 -pix_fmt yuv420p \
-i ${INPUT_FILE_RAW_420P} -filter_complex 'vpe_pp=low_res=(d2)' \
-c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264
```
## Balance transcoding quality and performance
Keywords:
 <font color="#0000dd"> -low_res "1:(d2)"
 -preset medium </font>

Sample:
```bash
ffmpeg -y -vsync 0 -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -low_res "1:(d2)" \
-transcode 1 -i ${INPUT_FILE_H264}
-c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264
```

## Multiple hardware device in one command
Codecs and filters can select different hardware devices:

```bash
ffmpeg -y -report -init_hw_device vpe=dev0:/dev/transcoder0 \
-init_hw_device vpe=dev1:/dev/transcoder3 \
-hwaccel auto -hwaccel_device dev1 -c:v h264_vpe -i test.h264 \
-filter_hw_device dev0 -filter_threads 4 -filter_complex_threads 4 \
-filter_complex "hwdownload,format=nv12[0];[0]split[a][b];\
[a]scale=720:1280:flags=lanczos,unsharp[a0];\
[a0]vpe_pp=outputs=1:low_res=(d2)[out0];\
[b]scale=540:960:flags=lanczos,unsharp[b0];\
[b0]vpe_pp=outputs=1:low_res=(d2)[out1]" \
-map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 1250k out0.mp4 \
-map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 600k out1.mp4
```

# 5.Use VPE in K8S

If you's are running VPE on Solios-X platform, please follow below link to know how to let it working under k8s:
https://github.com/VeriSilicon/solios-x-device-plugin