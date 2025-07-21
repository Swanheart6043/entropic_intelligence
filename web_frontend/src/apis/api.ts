import axios from "axios";
import { message } from "antd";
import type { Response, ResponseList, Control, Environmenta, ControlRequest } from "./apiType"

axios.defaults.baseURL = "http://192.168.1.100:8081";

axios.interceptors.request.use(async (requestConfig) => {
  return requestConfig;
});

axios.interceptors.response.use((response: any) => {
  const { success, errMsg } = response.data || {};
  if (success) return response;
  message.error(errMsg);
});

export const fetchControlData = async () => {
  const result = await axios.get<Response<Control>>('/api/control')
  return result.data
}

export const updateLamp = async (params: ControlRequest) => {
  const result = await axios.post<Response<null>>('/api/lamp', params)
  return result.data
}

export const updateSpeakers = async (params: ControlRequest) => {
  const result = await axios.post<Response<null>>('/api/speakers', params)
  return result.data
}

export const updateFan = async (params: ControlRequest) => {
  const result = await axios.post<Response<null>>('/api/fan', params)
  return result.data
}

export const fetchEnvironmentalData = async () => {
  const result = await axios.get<Response<Environmenta>>('/api/env')
  return result.data
}

export const fetchPhotoWall = async () => {
  const result = await axios.get<ResponseList<string>>('/api/photo')
  return result.data
}