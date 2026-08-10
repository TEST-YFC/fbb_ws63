---
title: Boot 移植应用开发指南
---

# 前言<a name="ZH-CN_TOPIC_0000001732354354"></a>

**概述<a name="section4537382116410"></a>**

本文档描述了WS63V100 RomBoot、LoaderBoot及FlashBoot工作流程，用户可参考此文档对FlashBoot进行二次开发。

# Boot简介<a name="ZH-CN_TOPIC_0000001779234629"></a>

WS63V100 Boot分为三部分：RomBoot、FlashBoot、LoaderBoot。

-   RomBoot功能包括：
    -   加载LoaderBoot到RAM，进一步利用LoaderBoot下载镜像到Flash，烧写efuse等。
    -   校验并引导FlashBoot。FlashBoot分为AB面，A面校验成功直接启动，校验失败会去校验B面，B面校验成功，则从B面启动，否则复位重启。

-   FlashBoot功能包括：
    -   升级固件。
    -   校验并引导固件。

-   LoaderBoot功能包括：
    -   下载镜像到Flash。
    -   烧写EFUSE（例如：安全启动/Flash加密相关密钥等）。

**图 1**  Boot启动流程<a name="fig2070413408573"></a>  
![](figures/Boot启动流程.png "Boot启动流程")

# RomBoot功能说明<a name="ZH-CN_TOPIC_0000001732195210"></a>



## 下载镜像及烧写EFUSE<a name="ZH-CN_TOPIC_0000001732195194"></a>

RomBoot通过加载Loaderboot实现下载镜像到Flash及烧写EFUSE的功能，具体操作请参见《WS63V100 BurnTool工具 使用指南》。

## 检验及引导Flashboot<a name="ZH-CN_TOPIC_0000001779394349"></a>

校验并引导FlashBoot流程如[图1](#fig1578634595518)所示。

**图 1**  校验并引导FlashBoot流程图<a name="fig1578634595518"></a>  
![](figures/校验并引导FlashBoot流程图.png "校验并引导FlashBoot流程图")

# LoaderBoot功能说明<a name="ZH-CN_TOPIC_0000001779394361"></a>

LoaderBoot是直接与BurnTool进行交互的组件，RomBoot无法直接实现烧写的功能，需要将LoaderBoot加载到RAM后，跳转到LoaderBoot，进一步通过LoaderBoot完成相关内容的烧写，LoaderBoot可烧写的内容包括：

-   FlashBoot
-   EFUSE参数配置文件
-   固件镜像（包括NV参数）
-   产测镜像

>![](public_sys-resources/icon-note.gif) **说明：** 
>LoaderBoot一般不涉及二次开发。

# FlashBoot说明<a name="ZH-CN_TOPIC_0000001732354346"></a>


## FlashBoot启动流程<a name="ZH-CN_TOPIC_0000001732195198"></a>

校验并引导固件流程如[图1](#fig921910011115)所示。

**图 1**  校验并引导固件流程图<a name="fig921910011115"></a>  
![](figures/校验并引导固件流程图.png "校验并引导固件流程图")

