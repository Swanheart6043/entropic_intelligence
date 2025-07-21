package com.example.demo.service;

import com.example.demo.mapper.InfoMapper;
import com.example.demo.model.Info;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;
import java.util.List;

@Service
public class InfoService {

    @Autowired
    private InfoMapper infoMapper;

    // 获取所有用户
    public String getAllUsers() {
//        return infoMapper.getAllUsers();
        return "hello";
    }

    // 根据 ID 获取用户
    public Info getUserById(int id) {
        return infoMapper.getUserById(id);
    }

    // 添加用户
    public void addUser(Info user) {
        infoMapper.addUser(user);
    }

    // 更新用户
    public void updateUser(Info user) {
        infoMapper.updateUser(user);
    }

    // 删除用户
    public void deleteUser(int id) {
        infoMapper.deleteUser(id);
    }
}