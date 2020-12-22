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
    * [HWuploader](#HWuploader)
* [3.FFmpeg Command Line Examples](#3.FFmpeg-Command-Line-Examples)
    * [Transcoding](#Transcoding)
    * [Decoding Only](#Decoding-Only)
    * [Encoding Only](#Encoding-Only)
    * [Transcoding with parameters](#Transcoding-with-parameters)
    * [Downscaling First Pass](#Downscaling-First-Pass)
* [4.Online Typical Use Case](#5.Online-Typical-Use-Case)
* [5.Use VPE in K8s](#4.Use-VPE-in-K8S)

# 1.Introducing

This project is VeriSilicon VPE plugin development trunk, it keep synced with FFmpeg master branch.

## Plugin Summary

| Plugin | Type| Comments| Capbilities|
|---------------|---------|---------------------------------------------------------------------------------------------------------------|----------------------------------------------------|
| vpe | device| FFmpeg device which be used by parameter “-init_hw_device”, for example “ffmpeg vpe=dev0:/dev/transcoder0” ||
| h264enc_vpe| encoder | VeriSilicon H264 hardware encoder interface | Maximum 4K 60Hz, High 10 Profile, levels 1 - 5.2 |
| hevcenc_vpe| encoder | VeriSilicon HEVC hardware encoder interface | Maximum 4K 60Hz, Main 10 Profile, levels 5.1|
| vp9enc_vpe | encoder | VeriSilicon VP9 hardware encoder interface| Maximum 4K 60Hz Profile 2 (10-bit)|
| h264_vpe | decoder | VeriSilicon H264 hardware decoder interface | Maximum 4K 60Hz, High 10 Profile, levels 1 - 5.2 |
| hevc_vpe | decoder | VeriSilicon HEVC hardware decoder interface | Maximum 4K 60Hz, Main 10 Profile, levels 5.1|
| vp9_vpe| decoder | VeriSilicon VP9 hardware decoder interface| Maximum 4K 60Hz Profile 2 (10-bit)|
| vpe_pp | filter| VeriSilicon Post Processing Filter| Upload raw data to hardware encoders, and doing the video downscaling. <br>Suppported raw data format:<br> YUV420P <br> YUV422P<br> NV12<br> NV21<br> YUV420P10LE<br>YUV420P10BE<br>YUV422P10LE<br>YUV422P10BE<br>P010LE<br>P010BE<br>YUV444P<br>RGB24<br>BGR24<br>ARGB<br>RGBA<br>ABGR<br>BGRA<br>|
| spliter_vpe| filter| VeriSilicon Spliter | Spliter input video to Maximum 4 paths |
| hwupload_vpe | filter| VeriSilicon Hardware Uploaderfor UYVY422| Upload raw data to hardware encoders. <br>Suppported raw data format:<br> UYVY422|

## Install VPE
1. Install VPE supported hardware like Solios-X to your computer;
2. Download VPE driver:
    ```bash
    # git clone git@github.com:VeriSilicon/vpe.git
    ```
3. Config tollchain:
    If you are doing cross-compiling, please run configure to config tollchain related setting.

    ```bash

    ./configure --arch=arm64 --cross=aarch64-linux-gnu- --sysroot=toolchain/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/aarch64-linux-gnu/libc --kernel=/work/imx8mmevk-poky-linux/linux-imx/4.19.35-r0/build

    arch=arm64
    cross=aarch64-linux-gnu-
    sysroot=/toolchain/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/aarch64-linux-gnu/libc
    kernel=/work/imx8mmevk-poky-linux/linux-imx/4.19.35-r0/build
    debug=n
    outpath=
    Create VPE build config file successfully!
    ```
4. Build VPE:
    then you can build VPE
    ```bash
    # make
    ```

5. Install VPE:
    * For non-cross build, the VPE libs will be installed to your build server;
    * For cross build, the VPE lib will be installed to --outpath folder which was specified by "--outpath" parameter in ./configure; if outpath is not specified, then the VPE libs will be copied to VPE/build folder

    ```bash
    # sudo make install
    ```
## Install FFmepg
1. Get FFmpeg source code:
    ```bash
    # git clone git@github.com:VeriSilicon/ffmpeg.git
    ```
2. Build FFmpeg - non-cross compiling
    ```bash
    # cd ffmpeg
    # ./configure --pkg-config=true --enable-vpe --extra-ldflags="-L/usr/lib/vpe" --extra-libs="-lvpi"
    # make -j8
    ```

2. Build FFmpeg - cross compiling:
    Please use vpe/build_vpe.sh to do this.
## Build FFmpeg + VPE together

We provide script to do this job, it's vpe/build_vpe.sh
This script will do VPE compiling, VPE installation, and FFmpeg compiling.
For cross-compiling case, this script is the best choise since the FFmpeg configuration  is a littler bit complex.
## Run FFmpeg
```bash
./ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -transcode 1 \
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
    ffmpeg -init_hw_device vpe=dev0:/dev/transcoder0,priority=live,vpeloglevel=0
    ```
## Decoder
| Option| Sub Option | Type | Description| Range | Default Value|
|---------------|------------|--------|-----------------------------------------------------------------------------|----------------------|------------------|
| -low_res||string | Set output streams number and set the downscaling size for each stream.<br><br>1. The suppported minimal window for H264/HEVC is [128, 98], for VP9 is [66, 66] <br><br>2. The target windows width and height should always equal or less than source video width and heigh | H264/HEVC: W>=128 H>=98<br><br>VP9: <br>W>=66 H>=66 |
| -dev ||string | Set device name||/dev/transcoder0 |
| -transcode ||int | Whether need doing transcoding, for decoder and encoder only case, this opition is not required.| 0,1|0|

Example：
Below example will do hevc->h264 transcoding and output two streams: one is orignal resolution h264 stream, the second one is 640x360 h264 stream.
```bash
ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v h264enc_vpe out0.h264 -map '[out1]' -c:v h264enc_vpe out1.h264
```

#### "low_res" formats

for decoder supports multiple formats, supporse orignal stream resolution is W x H:

1. Fixed resolution. Below example will output 4 streams: [W x H], [1920x1080], [1280x720], [640x360]:
> low_res=4:(1920x1080)(1280x720)(640x360)

2. Relative resolution. Below example will output 4 streams: [W x H], [960x540], [480x270], [640x360]
> low_res=4:(d2)(d4)(d8)

There is one special case: the downscaled stream is a non-output stream, it will be used as input of encoder to enhance the encoder quality, command line example please refer [Downscaling First Pass](#Downscaling-First-Pass)

3. Equal proportion: H or H will follow the scaling ratio.
 Below example will output 4 streams: [W x H], [(W x (1080/H)) x 1080], [(W x (720/H) x 720], [(W x (360/H) x 360]
> low_res=4:(-1x1080)(-1x720)(-1x360)

 Below example will output 4 streams: [W x H], [(1920 x (H*1920/W)], [1280 x (H*1280/W)], [640 x (H*640/W)]

> low_res=4:(1920x-1)(1280x-1)(640x-1)

4. Cropping and scaling: Two sets are required for each stream: scropping setting and scaling setting. IN below example, the first scropping window is (10,10,3840x2160), the scaling target resolution is 1920x1080
> low_res='2:(10,10,3840,2160,1920x1080)'

Note: low_res is also for vpe_pp filter, the only difference is only the streams numbers is not required in vpe_pp filer, for example:
> low_res=(1920x1080)(1280x720)(640x360).

## H264 HEVC Encoder

| Option | Sub Option| Type | Description| Range| Default Value|
|--------------------|-----------------|--------|----------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------|
| -b:v| | int| Target bitrate for rate control.| [10000...levelMax]| 1000000|
| -r (for input)| | int| Input picture rate numerator. | [1...1048575] | 30 |
||| int| Input picture rate denominator. | [1...1048575] | 1|
| -r (for output) | | int| Output picture rate numerator.| [1...1048575] | input |
||| int| Output picture rate denominator.| [1...1048575] | input |
| -profile:v| | int| Encode profile: <br><br>HEVC - main/main still picture/main10 <br>H264 - baseline/main/<br>high/high10 | <br>HEVC:[0-2]<br> H264:[9-12] | HEVC: 0 <br> H264: 10 |
| -level| | int| Encode level | For HEVC - <br>[1.0/2.0/2.1/3.0/<br>3.1/4.0/ 4.1/5.0/5.1] <br><br>For H264:<br>[1/1b/1.1/1.2/1.3/2/2.1<br>/2.2/3/3.1/3.2/4/4.1/<br>4.2/5/5.1/5.2] | HEVC: 5.1 <br>H264: 5.2|
| -crf| | int| VCE Constant rate factor mode. Works with lookahead turned on. | [-1...51]| -1|
| -force_idr| | int| If forcing keyframes, force them as IDR frames... | [0...1]| 0|
| -preset | | string | Encoding preset.| [superfast<br>fast<br>medium<br>slow<br>superslow] | fast|
| -enc_params | | | |
||intra_pic_rate| int| I frame interval in frames. If intra_pic_rate and force_idr are not specified, then there will be only one I frame be encoded. | [0...INT_MAX]| 0|
||bitrate_window | int| Bitrate window length in frames.| [1...300] | intra_pic_rate |
||intra_qp_delta| int| Intra QP delta, QP difference between target QP and intra frame QP. | [-51...51] | -5|
||qp_hdr | int| Initial target QP.| [-1...255] | 26 |
||qp_min | int| Minimum frame header QP for any slices. | [0...51]| 0|
||qp_max | int| Maximum frame header QP for any slices. | [0...51]| 51 |
||fixed_intra_qp| int| Fixed Intra QP. Use fixed QP value for every intra frame in stream.| [0...51]| 0|
||tier| int| Encoder tier | 0- main tier<br> 1- high tier| 0|
||byte_stream| int| Stream type.| 0 - NAL units;<br>1 - byte stream| 1|
||video_range| int| Video signal sample range value in Hevc stream. | 0 - Y in [16...235] <br>and Cb/Cr in [16...240] ; <br><br>1 - range in [0...255]| 1|
||sei | int| Enable SEI messages (buffering period + picture timing). | [0...INT_MAX]| 0|
||cabac | int| Select cavlc or cabac, only for H264 encoding| 0 - cavlc <br>1 - cabac | 1|
||slice_size | int| Slice size in number of CTU rows. 0 to encode each picture in one slice. | [0...height/ctu_size]| 0|
||bit_var_range_I| int| Percent variations over average bits per frame for I frame. | [10...10000]| 10000|
||bit_var_range_P| int| Percent variations over average bits per frame for P frame. | [10...10000]| 10000|
||bit_var_range_B| int| Percent variations over average bits per frame for B frame. | [10...10000]| 10000|
||pic_rc | int| Picture rate control enable.| 0 - OFF<br>1 - ON| 0|
||pic_rc_config | string| Picture rate config file. Plain text number is required. for example "100000".| [10000...60000000] | -b:v |
||ctb_rc | int| CTB QP adjustment mode for Rate Control and Subjective Quality. | [0...1] | 0|
||tol_ctb_rc_inter | float| Tolerance of Ctb Rate Control for INTER frames. | Float point number, <br> Min = targetPicSize/<br>(1+tolctb_rcInter)<br>Max = targetPicSize*<br>(1+tolctb_rcInter)]; <br><br>A negative number <br>means no bit rate limit<br> in Ctb Rc| 0.0 |
||tol_ctb_rc_intra | float| Tolerance of Ctb Rate Control for INTRA frames. | Float point number | -1.0 |
||ctb_row_qp_step| int| The maximum accumulated QP adjustment step per CTB Row allowed by Ctb Rate Control. | (ctbRowQpStep / Ctb_per_Row) <br>and limited by <br>maximum = 4 | H264: 4; <br>HEVC: 16|
||pic_qp_delta_range | int| Qp_Delta Range in Picture RC| [Min:Max] <br>Min - [-1...-10]<br>Max - [1...10] | Min:-2; Max:3 |
||hrd_conformance| int| Enable HRD conformance. Uses standard defined model to limit bitrate variance. | [0...1] | 0|
||cpb_size | int| HRD Coded Picture Buffer size in bits.| >0| 1000000|
||gop_size | int| This parameter controls P frame interval.| [0...8] <br>0 - adaptive size<br>1-7 -fixed size| 0|
||gop_lowdelay | int| Enable default lowDelay GOP configuration, only valid for GOP size <= 4. | [0...1] | 0|
||qp_min_I| int| qpMin for I Slice.| [0...51]| 0|
||qp_max_I| int| qpMax for I Slice.| [0...51]| 51 |
||bframe_qp_delta | int| QP difference between BFrame QP and target QP.| [-1...51]| -1|
||chroma_qp_offset| int| Chroma QP offset. | [-12...12] | 0|
||vbr | int| Enable variable Bit Rate Control by qpMin.| 0 - OFF<br>1 - ON| 0|
||user_data| string | SEI User data file name. <br>File is read and inserted as SEI message before first frame.|||
||intra_area | int| CTB coordinates specifying rectangular area of CTBs to force encoding in intra mode.| left : top : right : bottom| -1|
||ipcm1_area | int| CTB coordinates specifying rectangular area of CTBs to force encoding in IPCM mode. | left : top : right : bottom| -1|
||ipcm2_area | int| CTB coordinates specifying rectangular area of CTBs to force encoding in IPCM mode. | left : top : right : bottom| -1|
||const_chroma | int| Enable setting chroma a constant pixel value. | [0...1] | 0|
||const_cb | int| The constant pixel value for Cb.| 8 bit - [0...255], <br> 10 bit - [0...1023] | 8 bit - 128<br>10 bit - 512 |
||const_cr | int| The constant pixel value for Cr.| 8 bit - [0...255], <br> 10 bit - [0...1023] | 8 bit - 128, <br>10 bit - 512 |
||rdo_level| int| Programable HW RDO Level. | [1...3] | 1|
||ssim| int| Enable SSIM Calculation.| 0 - Disable<br>1 - Enable| 1|
||disable_vui_timing_info | int| Write VUI timing info in SPS. | 0 - Disable<br>1 - Enable| 1|
||lookahead_depth| int| Number of lookahead frames. | [0...40]| 0|
||force8bit| int| Force to output 8bit enable.| [0...1]| 0|
||mastering_display_en|int|Mastering display colour stream| [0...1] | 0|
||display_pri_x0 | int |Green display primary x| [0...50000]| 0|
||display_pri_y0 | int |Green display primary y| [0...50000]| 0|
||display_pri_x1 | int |Blue display primary x | [0...50000]| 0|
||display_pri_y1 | int |Blue display primary y | [0...50000]| 0|
||display_pri_x2 | int |Red display primary x| [0...50000]| 0|
||display_pri_y2 | int |Red display primary y | [0...50000]| 0|
||white_point_x | int |White point x| [0...50000]| 0|
||white_point_y | int |White point y | [0...50000]| 0|
||min_luminance | int |Min display mastering luminance| [0...2^32 - 1]| 0|
||max_luminance | int |Max display mastering luminance| [0...2^32 - 1]| 0|
||light_level_en | int |Content light level enable| [0...1]| 0|
||max_content_light_level | int |Max content light leve | [0...2^16 - 1]| 0|
||max_pic_average_light_level | int |Max pic average light level| [0... 2^16 - 1]| 0|

Example:
> -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60"

## VP9 Encoder

| Option | Sub Option| Type | Description| Range | Default Value|
|--------------------|-----------------|--------|----------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------|
| -b:v | | int |Target bitrate for rate control. | [10000...60000000]| 1000000|
| -r (for input) | | int| Input picture rate numerator.| [1...1048575] | 30 |
| | | int| Input picture rate denominator.| [1...1048575] | 1|
| -r (for output)| | int| Output picture rate numerator. | [1...1048575] | inputRateNumer |
| | | int| Output picture rate denominator. | [1...1048575] | inputRateDenom |
| -effort | | int| Encoder effort level.| [0...5]<br> 0 - fastest <br>5 - best quality | 0|
| -lag_in_frames||int| Number of frames to lag. Up to 25.| [0...25]| 7|
| -passes | | int| Number of passes.| [1...2] | 1|
| -profile:v | | int| Encoder profile | [0...3] | 0|
| -preset | | string | Encoding preset.| superfast<br>fast<br>medium<br>slow<br>superslow | fast |
| -enc_params
| | effort| int| Encoder effort level.| [0...5]<br> 0 - fastest <br>5 - best quality | 0|
| | lag_in_frames | int| Number of frames to lag. Up to 25.| [0...25]| 7|
| | passes| int| Number of passes.| [1...2] | 1|
| | intra_pic_rate| int| Intra picture rate in frames.| [0...INT_MAX]| 0|
| | bitrate_window | int| Bitrate window length in frames. | [1...300] | 150|
| | qp_hdr | int| Initial QP used for the first frame. | [-1...255] | -1|
| | qp_min | int| Minimum frame header QP. | [0...255] | 10 |
| | qp_max | int| Maximum frame header QP. | [0...255] | 255|
| | fixed_intra_qp| int| Fixed Intra QP, 0 = disabled.| [0...255] | 0|
| | pic_rc | int| Picture rate control enable. | 0 - OFF<br>1 - ON| 1|
| | mcomp_filter_type | int| Interpolation filter mode. | [0...4] | 4|
| | force8bit | int| Force to output 8bit stream | [0...1] | 0|
| | ref_frame_scheme| int| Reference frame update scheme. | [0...4] | 4|
| | filte_level | int| Filter strength level for deblocking| [0...64]| 64 |
| | filter_sharpness | int| Filter sharpness level for deblocking. | [0...8], 8=auto | 8|

Example:
> -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60"

# Preset detail parameters:
| Level | format | Parameters setting| comment |
|-----------|--------|-------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------|
| superfast | h264 |  <br>--intra_qp_delta=-2 <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--ctb_rc=0 <br>--gop_size=1 | |
| | hevc |  <br>--intra_qp_delta=-2 <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--ctb_rc=0 <br>--gop_size=1 <br>--rdo_level=1| |
| | vp9|  <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--effort=0 <br>--mcomp_filter_type=4 <br>--ref_frame_scheme=0 <br>--lag_in_frames=0| |
| fast| h264 |  <br>--intra_qp_delta=-2 <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--ctb_rc=0 <br>--gop_size=4 ||
| | hevc |  <br>--intra_qp_delta=-2 <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--ctb_rc=0 <br>--gop_size=4 <br>--rdo_level=1| |
| | vp9|  <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--effort=0 <br>--mcomp_filter_type=4 <br>--ref_frame_scheme=4 <br>--lag_in_frames=7| |
| medium| h264 |  <br>--intra_qp_delta=-2 <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--ctb_rc=0 <br>--gop_size=4 <br>--lookahead_depth=20 | |
| | hevc |  <br>--intra_qp_delta=-2 <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--ctb_rc=0 <br>--gop_size=4 <br>--rdo_level=1 <br>--lookahead_depth=20| |
| | vp9|  <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--effort=0 <br>--mcomp_filter_type=4 <br>--ref_frame_scheme=4 <br>--passes=2 <br>--lag_in_frames=12 | |
| slow| h264 |  <br>--intra_qp_delta=-2 <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--ctb_rc=0 <br>--gop_size=0 <br>--lookahead_depth=30 | adaptive GOP size |
| | hevc |  <br>--intra_qp_delta=-2 <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--ctb_rc=0 <br>--gop_size=0 <br>--rdo_level=2 <br>--lookahead_depth=30| adaptive GOP size |
| | vp9|  <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--effort=1 <br>--mcomp_filter_type=4 <br>--ref_frame_scheme=4 <br>--passes=2 <br>--lag_in_frames=25 ||
| superslow | h264 |  <br>--intra_qp_delta=-2 <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--ctb_rc=0 <br>--gop_size=0 <br>--lookahead_depth=40 ||
| | hevc |  <br>--intra_qp_delta=-2 <br>--qp-hdr=-1 <br>--pic_rc=1 <br>--ctb_rc=0 <br>--gop_size=0 <br>--rdo_level=3 <br>--lookahead_depth=40||
| | vp9|  <br>--qp_hdr=-1 <br>--pic_rc=1 <br>--effort=2 <br>--mcomp_filter_type=4 <br>--ref_frame_scheme=4 <br>--passes=2 <br>--lag_in_frames=25 ||

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

## HWuploader
| Option | Sub Option | Type | Description | Range | Default Value |
|--------|------------|------|-------------|----------------------|---------------|
| /||/| / | /| / |
#

# 3.FFmpeg Command Line Examples

## Transcoding

| Case | Target | Source | Output | 2 Pass | Command Line Example|
|------|--------|--------|------------|----------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format | Numbers| Encoding | |
| 1| h264 | h264 | 1| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -i ${INPUT_FILE_H264} -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264|
| 2| h264 | h264 | 2| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset fast -b:v 5000000 out1.h264|
| 3| h264 | hevc | 3| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset fast -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset fast -b:v 3000000 out2.h264 |
| 4| h264 | hevc | 4| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset fast -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset fast -b:v 3000000 out2.h264 -map '[out3]' -c:v h264enc_vpe -preset fast -b:v 500000 out3.h264|
| 5| h264 | vp9| 1| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -i ${INPUT_FILE_VP9} -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264 |
| 6| h264 | vp9| 2| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v h264enc_vpe -preset fast -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset fast -b:v 5000000 out1.h264|
| 7| hevc | h264 | 3| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset fast -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset fast -b:v 3000000 out2.hevc |
| 8| hevc | h264 | 4| N|ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset fast -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset fast -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset fast -b:v 500000 out3.hevc |
| 9| hevc | hevc | 1| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -i ${INPUT_FILE_HEVC} -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc|
| 10 | hevc | hevc | 2| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset fast -b:v 5000000 out1.hevc|
| 11 | hevc | vp9| 3| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset fast -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset fast -b:v 3000000 out2.hevc |
| 12 | hevc | vp9| 4| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset fast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset fast -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset fast -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset fast -b:v 500000 out3.hevc|
| 13 | vp9| h264 | 1| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -i ${INPUT_FILE_H264} -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf|
| 14 |||2| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "2:(360x640)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset fast -b:v 5000000 out1.ivf|
| 15 | vp9| hevc | 3| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset fast -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset fast -b:v 3000000 out2.ivf |
| 16 | vp9| hevc | 4| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset fast -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset fast -b:v 3000000 out2.ivf -map '[out3]' -c:v vp9enc_vpe -preset fast -b:v 500000 out3.ivf|
| 17 | vp9| vp9| 1| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -i ${INPUT_FILE_VP9} -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf|
| 18 | vp9| vp9| 2| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v vp9enc_vpe -preset fast -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset fast -b:v 5000000 out1.ivf|
| 19 | h264 | h264 | 1| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -i ${INPUT_FILE_H264} -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264|
| 20 | h264 | h264 | 2| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset medium -b:v 5000000 out1.h264|
| 21 | h264 | hevc | 3| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset medium -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset medium -b:v 3000000 out2.h264 |
| 22 | h264 | hevc | 4| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset medium -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset medium -b:v 3000000 out2.h264 -map '[out3]' -c:v h264enc_vpe -preset medium -b:v 500000 out3.h264|
| 23 | h264 | vp9| 1| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -i ${INPUT_FILE_VP9} -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264|
| 24 | h264 | vp9| 2| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset medium -b:v 5000000 out1.h264|
| 25 | hevc | h264 | 3| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset medium -b:v 3000000 out2.hevc |
| 26 | hevc | h264 | 4| Y|ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset medium -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset medium -b:v 500000 out3.hevc |
| 27 | hevc | hevc | 1| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -i ${INPUT_FILE_HEVC} -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc|
| 28 | hevc | hevc | 2| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc|
| 29 | hevc | vp9| 3| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset medium -b:v 3000000 out2.hevc |
| 30 | hevc | vp9| 4| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset medium -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset medium -b:v 500000 out3.hevc|
| 31 | vp9| h264 | 1| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -i ${INPUT_FILE_H264} -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf|
| 32 | vp9| h264 | 2| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -transcode 1 -low_res "2:(360x640)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset medium -b:v 5000000 out1.ivf|
| 33 | vp9| hevc | 3| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset medium -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset medium -b:v 3000000 out2.ivf |
| 34 | vp9| hevc | 4| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset medium -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset medium -b:v 3000000 out2.ivf -map '[out3]' -c:v vp9enc_vpe -preset medium -b:v 500000 out3.ivf|
| 35 | vp9| vp9| 1| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -i ${INPUT_FILE_VP9} -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf|
| 36 | vp9| vp9| 2| Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -transcode 1 -low_res "2:(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -c:v vp9enc_vpe -preset medium -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset medium -b:v 5000000 out1.ivf|
| 37 | VP9| HEVC | 1| N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -transcode 1 -low_res "2:(d2)" -r 30 -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=2[out0][out1]' -map '[out0]' -f null /dev/null -map '[out1]' -c:v vp9enc_vpe -preset medium -b:v 5000000 out1.ivf

## Decoding Only

| Case | Target | Source | Output| 2 Pass | Command Line Example |
|------|--------|--------|---------|----------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format | Numbers | Encoding ||
| 38 | nv12 | h264 | 4 | NA | ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v h264_vpe -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=4[1][2][3][4],[1]hwdownload,format=nv12[a],[2]hwdownload,format=nv12[b],[3]hwdownload,format=nv12[c],[4]hwdownload,format=nv12[d]' -map '[a]' out0.yuv -map '[b]' out1.yuv -map '[c]' out2.yuv -map '[d]' out3.yuv |
| 39 | nv12 | hevc | 4 | NA | ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v hevc_vpe -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_HEVC} -filter_complex 'spliter_vpe=outputs=4[1][2][3][4],[1]hwdownload,format=nv12[a],[2]hwdownload,format=nv12[b],[3]hwdownload,format=nv12[c],[4]hwdownload,format=nv12[d]' -map '[a]' out0.yuv -map '[b]' out1.yuv -map '[c]' out2.yuv -map '[d]' out3.yuv |
| 40 | nv12 | vp9| 4 | NA | ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -c:v vp9_vpe -low_res "4:(1920x1080)(1280x720)(640x360)" -i ${INPUT_FILE_VP9} -filter_complex 'spliter_vpe=outputs=4[1][2][3][4],[1]hwdownload,format=nv12[a],[2]hwdownload,format=nv12[b],[3]hwdownload,format=nv12[c],[4]hwdownload,format=nv12[d]' -map '[a]' out0.yuv -map '[b]' out1.yuv -map '[c]' out2.yuv -map '[d]' out3.yuv |

## Encoding Only

| Case | Target | Source| Output| 2 Pass | Command Line Example|
|------|--------|---------|---------|----------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format| Numbers | Encoding | |
| 41 | h264 | yuv420p | 4 | N| ffmpeg -y -v 48 -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -s 1920x1080 -pix_fmt yuv420p -i ${INPUT_FILE_RAW_420P} -filter_hw_device "dev0" -filter_complex 'vpe_pp=outputs=4:low_res=(1920x1080)(1280x720)(640x360),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset superfast -b:v 10000000 out0.h264 -map '[out1]' -c:v hevcenc_vpe -preset superfast -b:v 5000000 out1.h264 -map '[out2]' -c:v hevcenc_vpe -preset superfast -b:v 3000000 out2.h264 -map '[out3]' -c:v hevcenc_vpe -preset superfast -b:v 500000 out3.h264 |
| 42 | hevc | yuv420p | 4 | N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -s 1920x1080 -pix_fmt yuv420p -i ${INPUT_FILE_RAW_420P} -filter_hw_device "dev0" -filter_complex 'vpe_pp=outputs=4:low_res=(1920x1080)(1280x720)(640x360),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset superfast -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset superfast -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset superfast -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset superfast -b:v 500000 out3.hevc|
| 43 | vp9| yuv420p | 4 | N| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -r 30 -s 1920x1080 -pix_fmt yuv420p -i ${INPUT_FILE_RAW_420P} -filter_hw_device "dev0" -filter_complex 'vpe_pp=outputs=4:low_res=(1920x1080)(1280x720)(640x360),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v vp9enc_vpe -preset superfast -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset superfast -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset superfast -b:v 3000000 out2.ivf -map '[out3]' -c:v vp9enc_vpe -preset superfast -b:v 500000 out3.ivf |
| 44 | h264 | rgb24 | 4 | Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -r 30 -s 1920x1080 -pix_fmt rgb24 -i ${INPUT_FILE_RAW_RGB24} -filter_hw_device "dev0" -filter_complex 'vpe_pp=outputs=4:low_res=(d2)(d4)(d8),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v h264enc_vpe -preset medium -b:v 10000000 out0.h264 -map '[out1]' -c:v h264enc_vpe -preset medium -b:v 5000000 out1.h264 -map '[out2]' -c:v h264enc_vpe -preset medium -b:v 3000000 out2.h264 -map '[out3]' -c:v h264enc_vpe -preset medium -b:v 500000 out3.h264|
| 45 | hevc | abgr| 4 | Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -r 30 -s 1920x1080 -pix_fmt abgr -i ${INPUT_FILE_RAW_ABGR} -filter_hw_device "dev0" -filter_complex 'vpe_pp=outputs=4:force10bit=1:low_res=(d2)(d4)(d8),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v hevcenc_vpe -preset medium -b:v 10000000 out0.hevc -map '[out1]' -c:v hevcenc_vpe -preset medium -b:v 5000000 out1.hevc -map '[out2]' -c:v hevcenc_vpe -preset medium -b:v 3000000 out2.hevc -map '[out3]' -c:v hevcenc_vpe -preset medium -b:v 500000 out3.hevc |
| 46 | vp9| rgba| 4 | Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0,priority=vod,vpeloglevel=0 -r 30 -s 1920x1080 -pix_fmt rgba -i ${INPUT_FILE_RAW_RGBA} -filter_hw_device "dev0" -filter_complex 'vpe_pp=outputs=4:low_res=(-1x1080)(-1x674)(-1x336),spliter_vpe=outputs=4[out0][out1][out2][out3]' -map '[out0]' -c:v vp9enc_vpe -preset slow -b:v 10000000 out0.ivf -map '[out1]' -c:v vp9enc_vpe -preset slow -b:v 5000000 out1.ivf -map '[out2]' -c:v vp9enc_vpe -preset slow -b:v 3000000 out2.ivf -map '[out3]' -c:v vp9enc_vpe -preset slow -b:v 500000 out3.ivf|

## Transcoding with parameters

| Case | Target | Source | Output| 2 Pass | Command Line Example|
|------|--------|--------|---------|----------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format | Numbers | Encoding | |
| 47 | h264 | h264 | 3 | Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 10000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out0.h264 -map '[out1]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 5000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out1.h264 -map '[out2]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 3000000 -enc_params "lookahead_depth=10:chroma_qp_offset='2':bitrate_window=180:intra_pic_rate=60" out2.h264 |
| 48 | h264 | argb | 4 | Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0 -s 1920x1080 -pix_fmt argb -i ${INPUT_FILE_RAW_ARGB} -filter_hw_device "dev0" -filter_complex "vpe_pp=outputs=4:force10bit=1:low_res=(1920x1080)(1280x720)(640x360),spliter_vpe=outputs=4[out0][out1][out2][out3]" -map '[out0]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 10000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out0.h264 -map '[out1]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 5000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out1.h264 -map '[out2]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 3000000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out2.h264 -map '[out3]' -c:v h264enc_vpe -profile:v main -level 5.2 -b:v 500000 -enc_params "lookahead_depth=10:chroma_qp_offset=2:bitrate_window=180:intra_pic_rate=60" out3.h264 |
| 49 | vp9| h264 | 3 | Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -transcode 1 -low_res "3:(1280x720)(640x360)" -i ${INPUT_FILE_H264} -filter_complex 'spliter_vpe=outputs=3[out0][out1][out2]' -map '[out0]' -c:v vp9enc_vpe -b:v 10000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out0.ivf -map '[out1]' -c:v vp9enc_vpe -b:v 5000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out1.ivf -map '[out2]' -c:v vp9enc_vpe -b:v 3000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out2.ivf |
| 50 | vp9| nv12 | 4 | Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0 -s 1920x1080 -pix_fmt nv12 -i ${INPUT_FILE_RAW_NV12} -filter_hw_device "dev0" -filter_complex "vpe_pp=outputs=4:low_res=(1920x1080)(1280x720)(640x360),spliter_vpe=outputs=4[out0][out1][out2][out3]" -map '[out0]' -c:v vp9enc_vpe -r 60 -b:v 10000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out0.ivf -map '[out1]' -c:v vp9enc_vpe -b:v 5000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out1.ivf -map '[out2]' -c:v vp9enc_vpe -b:v 3000000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out2.ivf -map '[out3]' -c:v vp9enc_vpe -b:v 500000 -enc_params "ref_frame_scheme=4:lag_in_frames=18:passes=2:bitrate_window=60:effort=0:intra_pic_rate=60" out3.ivf |

## Downscaling First Pass

The downscaled video will be used as the input of encoder to do second pass encoding; hence in this case the encoder quality will be better than first pass encoding.

| Case | Target | Source | Output| 2 Pass | Command Line Example |
|------|--------|--------|---------|----------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ID | Format | Format | Numbers | Encoding ||
| 51 | h264 | hevc | 1 | Y| ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0 -c:v hevc_vpe -transcode 1 -low_res "1:(d2)" -i ${INPUT_FILE_HEVC} -c:v h264enc_vpe-preset medium -b:v 10000000 out0.h264 |

# 4.Online Typical Use Case
## Capture Camera and start RTP streaming:
###### Streaming:
```bash
sudo ./ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0 -i /dev/video0 -filter_hw_device "dev0" -filter_complex "hwupload_vpe"
-c:v h264enc_vpe -preset fast -b:v 500000
-enc_params "intra_pic_rate=15" -f rtp_mpegts rtp://10.10.3.88:9999
```
###### Play
```bash
ffplay rtp://10.10.3.88:9999
```
## Capture RTMP straming, do transcoding, streamning to another RTMP server:
You need to setup RTMP streaming server first
###### Streaming:
```bash
./ffmpeg -y -re -init_hw_device vpe=dev0:/dev/transcoder0 -c:v h264_vpe -transcode 1 -i http://ivi.bupt.edu.cn/hls/cctv5phd.m3u8
-c:v h264enc_vpe -preset medium -b:v 3000000 -level 5.1 -profile:v high -c:a copy
-f flv rtmp://10.10.3.88:1935/live/livestream
```
###### Play
```bash
ffplay rtmp://10.10.3.88:1935/live/livestream
```

## Downscale 4K HEVC streaming to H264 1080p, and streamning it to RTMP server:
You need to setup RTMP streaming server first
###### Streaming:
```bash
./ffmpeg -y -re -init_hw_device vpe=dev0:/dev/transcoder0 -c:v hevc_vpe -transcode 1 -low_res "2:(1920x1080)" \
-i ~/work/stream/LaLaLand_cafe_4K.mkv -filter_complex 'spliter_vpe=outputs=2[out0][out1]' \
-map '[out0]' -f null /dev/null -map '[out1]' -c:v h264enc_vpe \
-enc_params "intra_pic_rate=15" -preset medium -b:v 2000000 \
-map 0:a -f flv rtmp://10.10.3.88:1935/live/livestream
```
###### Play
```bash
ffplay rtmp://10.10.3.88:1935/live/livestream
```

## Downscale 4K HEVC streaming to 4 small stream, and streamning it to RTMP server:
You need to setup RTMP streaming server first
###### Streaming:
```bash
./ffmpeg -y -re -init_hw_device vpe=dev0:/dev/transcoder0 -c:v hevc_vpe -transcode 1 \
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

## Encode HDR10 stream:

```bash
./ffmpeg -y -report -init_hw_device vpe=dev0:/dev/transcoder0 \
-s 3840x2160 -pix_fmt yuv420p10le -i 4k10bithdr.yuv -filter_hw_device "dev0" \
-filter_complex "hwupload_vpe" -c:v hevcenc_vpe  \
-color_primaries bt2020 -color_trc smpte2084 -colorspace bt2020nc \
-enc_params "mastering_display_en=1:display_pri_x0=13250:display_pri_y0=34500:display_pri_x1=7500:display_pri_y1=3000:display_pri_x2=34000:display_pri_y2=16000:white_point_x=15635:white_point_y=16450:min_luminance=0:max_luminance=10000000:light_level_en=1:max_content_light_level=1000:max_pic_average_light_level=640" 4khdr.hevc
```

## Enable dynamic bitrate change through config file:
Need to add "pic_rc=1:pic_rc_config=rc.cfg" into -enc_params, then user can change file "rc.cfg" to put target bitrate:
```bash
sudo ./ffmpeg -y -init_hw_device vpe=dev0:/dev/transcoder0 -i /dev/video0 -filter_hw_device "dev0"
-filter_complex "hwupload_vpe" -c:v h264enc_vpe -preset fast -b:v 500000
-enc_params "intra_pic_rate=15:pic_rc=1:pic_rc_config=rc.cfg"
-f rtp_mpegts rtp://10.10.3.88:9999
echo 300000 > rc.cfg
```

# 5.Use VPE in K8S

If you's are running VPE on Solios-X platform, please follow below link to know how to let it working under k8s:
https://github.com/VeriSilicon/solios-x-device-plugin
