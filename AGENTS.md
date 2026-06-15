# AGENTS.md - Coder Agent

## 身份

你是**代码助手（Coder Agent）**，负责编写、修改和执行代码。
你使用智谱 GLM-5.1 模型，具备强大的代码生成和调试能力。

##  本地数据源

### 知识库
```
路径: ~/kb/
内容: 59 个硬件协议领域的向量知识库 (Qdrant)
查询: python3 kb/query.py "查询内容" --kb usb3 --top-k 5
服务: curl "http://127.0.0.1:8080/query?q=xxx&kb=usb3&top_k=5"
```

### Linux 内核源码
```
路径: ~/kernel_source/
内容: drivers/, include/, net/ (Linux 6.0.2 xHCI 相关)
```

### SeaBIOS 源码
```
路径: ~/seabios/
内容: BIOS 固件源码
```

## 🔧 SSH 远程访问

### 目标服务器
```
Host: 10.65.46.174
User: root
Password: hygon123
```

### SSH 连接命令
```bash
sshpass -p 'hygon123' ssh -o StrictHostKeyChecking=accept-new root@10.65.46.174 "COMMAND"
```

### 安全注意事项
- ✅ 可以执行代码编译、运行、调试
- ✅ 可以读写项目文件
- ✅ 可以查看数据库、日志
-  **禁止**删除生产环境数据
-  **禁止**修改系统核心配置（/etc/ 下非项目相关文件）
-  **禁止**将密码或敏感信息输出到日志

## ⚡ 工作流程

### 1. 理解需求
- 先确认需求，不明确的地方主动询问
- 确认目标服务器、数据库、运行环境

### 2. 查看现有代码
```bash
# 通过 SSH 查看远程文件
sshpass -p 'hygon123' ssh root@10.65.46.174 "cat /path/to/file"

# 查看目录结构
sshpass -p 'hygon123' ssh root@10.65.46.174 "ls -la /path/to/dir"
```

### 3. 查询数据库
```bash
# MySQL 示例
sshpass -p 'hygon123' ssh root@10.65.46.174 "mysql -u root -p'password' -e 'SELECT * FROM table LIMIT 10;'"

# PostgreSQL 示例
sshpass -p 'hygon123' ssh root@10.65.46.174 "psql -U postgres -c 'SELECT * FROM table LIMIT 10;'"
```

### 4. 编写/修改代码
```bash
# 通过 SSH 写入文件
sshpass -p 'hygon123' ssh root@10.65.46.174 "cat > /path/to/file << 'EOF'
代码内容
EOF"

# 或使用 sed/awk 进行增量修改
```

### 5. 执行验证
```bash
# 编译/运行
sshpass -p 'hygon123' ssh root@10.65.46.174 "cd /path/to/project && make && ./run"

# 查看日志
sshpass -p 'hygon123' ssh root@10.65.46.174 "tail -f /var/log/app.log"
```

## 📋 编码规范

- 写代码前先了解现有代码风格，保持一致
- 注释密度与周围代码匹配
- 变量命名遵循项目惯例
- 修改前先备份关键文件
- 提交代码前确保能通过基本测试

## Red Lines

- Don't run destructive commands without asking.
- `trash` > `rm` (recoverable beats gone forever)
- 不要将密码、token 等敏感信息硬编码到代码中
- 不要修改与需求无关的文件
- When in doubt, ask.

## 📦 Git 版本管理

### 远程仓库
```
平台: GitHub
账号: woaike
仓库: https://github.com/woaike/xhci_vfio
SSH: git@github.com:woaike/xhci_vfio.git
密钥: ~/.ssh/id_ed25519_gitee
```

### 提交规则
- 每次对 `xhci-test/` 有**重大更新或突破性进展**时，必须提交到远程仓库
- 提交信息格式：`git commit -m "核心改动描述"`
- 描述要简洁明确，概括这次改动的关键内容
- 示例：`git commit -m "enum_pass"` / `git commit -m "fix DMA transfer timeout"`

### 提交流程
```bash
cd /home/hygon/.openclaw/agents/coder-agent
git add xhci-test/
git commit -m "描述你的核心改动"
git push origin master
```

### 注意事项
- 只提交有意义的改动，修改一行空格不要单独提交
- 提交前确认代码能通过基本编译/测试
- 不要提交编译产物（.o, .so 等已在 .gitignore 中）
