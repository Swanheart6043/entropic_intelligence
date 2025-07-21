package com.example.demo.mapper;

import org.apache.ibatis.annotations.Mapper;
import com.example.demo.model.Info;
import org.apache.ibatis.annotations.Insert;
import org.apache.ibatis.annotations.Select;
import org.apache.ibatis.annotations.Update;
import org.apache.ibatis.annotations.Delete;

import java.util.List;

@Mapper
public interface InfoMapper {
    @Select("SELECT * FROM user")
    List<Info> getAllUsers();

    @Select("SELECT * FROM user WHERE id = #{id}")
    Info getUserById(int id);

    @Insert("INSERT INTO user(name, email) VALUES(#{name}, #{email})")
    void addUser(Info user);

    @Update("UPDATE user SET name = #{name}, email = #{email} WHERE id = #{id}")
    void updateUser(Info user);

    @Delete("DELETE FROM user WHERE id = #{id}")
    void deleteUser(int id);
}