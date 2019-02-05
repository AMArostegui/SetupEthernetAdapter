# SetupEthernetAdapter
Programatically Configure an Ethernet Network Adapter on a Windows Machine Using C/MFC

I'm working on a commercial software related to an industrial field that happens to require specific network settings.

For reliability, we aim our software to work only with an ethernet adapter on a harsh environment but we don't really control the kind of computer our customers are using.

Nowadays, computers come with a variety of connection devices besides an ethernet card, such as bluetooth or wi-fi adapters. To make things worse, some programs like WMWare create virtual adapters for their own use. We need to automatically find a physical ethernet card and enforce a given set of parameters for our program to work properly.

The industrial field runs at a very different pace than software development and change happens slowly. That makes it necessary to support older OS like Windows XP as they are seldom updated, since most of the times the computers are not even connected to the internet.

As the code changes system settings, administrator privileges are required.
