package com.example.demo.controller;

import com.example.demo.model.Info;
import com.example.demo.service.InfoService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.bind.annotation.*;
import java.util.List;

@RestController
@RequestMapping("/info")
public class InfoController {

    @Autowired
    private InfoService infoService;

    // 获取所有用户
    @GetMapping
    public String getAllUsers() {
        return infoService.getAllUsers();
    }

    // 根据 ID 获取用户
    @GetMapping("/{id}")
    public Info getUserById(@PathVariable int id) {
        return infoService.getUserById(id);
    }

    // 添加用户
    @PostMapping
    public void addUser(@RequestBody Info user) {
        infoService.addUser(user);
    }

    // 更新用户
    @PutMapping("/{id}")
    public void updateUser(@PathVariable int id, @RequestBody Info user) {
        user.setId(id);  // 确保更新的用户 ID 是正确的
        infoService.updateUser(user);
    }

    // 删除用户
    @DeleteMapping("/{id}")
    public void deleteUser(@PathVariable int id) {
        infoService.deleteUser(id);
    }
}