#!/bin/bash
sudo systemctl restart web_gateway.service
sudo systemctl restart task_center.service
sudo systemctl restart monitor_center.service
sudo systemctl restart edge_gateway.service
sudo systemctl restart audio_video_center.service