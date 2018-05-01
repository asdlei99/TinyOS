# TinyOS

学习用小型操作系统，运行在x86（32位）计算机上。

---

## 特性

- [x] 分页和缺页中断处理
- [x] 内核线程
- [x] 信号量
- [x] 进程管理
- [x] 系统调用
- [x] 键盘驱动
- [x] 消息传递
- [x] 硬盘驱动
- [ ] 文件系统
- [ ] 终端

## 运行

makefile仅对在bochs虚拟机上的运行有所支持。要使用bochs运行该系统，需完成以下步骤：

1. 安装bochs，版本不低于2.68。
2. 跟据bochs所附带的显示插件，修改`bochsrc.txt`中的`display_library`项。
3. 在项目根目录下运行命令`mkdir tools`，创建外部工具输出目录。
4. 在项目根目录下运行命令`bximage`，创建一个大小为128MB、名为`hd.img`、模式为`flat`的硬盘映像文件。
5. 运行命令`make bochs`以构建项目并启动虚拟机。
