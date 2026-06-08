#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <functional>
#include <Windows.h>

namespace tauricpp {

/// 虚拟文件系统 - 在内存中管理前端资源
/// 资源从exe的资源段加载，或通过API注入，不产生任何临时文件
class VirtualFS {
public:
    struct VFile {
        std::vector<uint8_t> data;
        std::string mime_type;
    };

    static VirtualFS& Instance();

    // 禁止拷贝和移动
    VirtualFS(const VirtualFS&) = delete;
    VirtualFS& operator=(const VirtualFS&) = delete;

    /// 注册一个虚拟文件
    void RegisterFile(const std::string& path, const std::vector<uint8_t>& data, const std::string& mime_type);

    /// 注册一个虚拟文件（字符串版本）
    void RegisterFile(const std::string& path, const std::string& content, const std::string& mime_type);

    /// 注册一个虚拟文件（右值引用版本，避免大文件拷贝）
    void RegisterFile(const std::string& path, std::vector<uint8_t>&& data, const std::string& mime_type);

    /// 查找虚拟文件（返回指针，不拷贝数据；返回nullptr表示未找到）
    const VFile* FindFile(const std::string& path) const;

    /// 查找虚拟文件（拷贝到out，兼容旧接口）
    bool FindFile(const std::string& path, VFile& out) const;

    /// 从exe资源段加载所有前端资源
    /// 资源格式：自定义资源类型 "TAURI_RES"，每个资源ID对应一个虚拟路径
    void LoadFromResources(HMODULE hModule);

    /// 从文件系统目录加载到内存（开发模式用）
    /// @param dir_path 目录路径，所有文件递归加载到VirtualFS
    void LoadFromDirectory(const std::string& dir_path);

    /// 清空所有虚拟文件
    void Clear();

    /// 获取所有已注册的虚拟路径
    std::vector<std::string> GetAllPaths() const;

private:
    VirtualFS() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, VFile> files_;

    /// 根据文件扩展名推断MIME类型
    static std::string InferMimeType(const std::string& path);
};

} // namespace tauricpp
