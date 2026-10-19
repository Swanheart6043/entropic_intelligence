#!/bin/bash
sudo systemctl stop web_gateway.service
sudo systemctl stop task_center.service
sudo systemctl stop monitor_center.service
sudo systemctl stop edge_gateway.service
sudo systemctl stop audio_video_center.service