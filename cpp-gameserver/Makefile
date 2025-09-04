# 简单的Makefile - 用于快速开始
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -O2 -g
LDFLAGS = -pthread

# 源文件
SOURCES = src/simple_echo_server.cpp
EXECUTABLE = echo_server

# 默认目标
all: $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(EXECUTABLE) $(LDFLAGS)
	@echo "\n✅ 编译成功！"
	@echo "运行: ./$(EXECUTABLE) [端口]"
	@echo "默认端口: 7777\n"

# 运行服务器
run: $(EXECUTABLE)
	./$(EXECUTABLE)

# 清理
clean:
	rm -f $(EXECUTABLE)
	@echo "清理完成"

# 测试客户端
test-client:
	@echo "正在连接到服务器..."
	@nc localhost 7777

# 压力测试（需要安装wrk）
stress-test:
	@echo "开始压力测试..."
	@for i in {1..100}; do \
		(echo "Hello from client $$i"; sleep 1; echo "quit") | nc localhost 7777 & \
	done
	@wait
	@echo "压力测试完成"

.PHONY: all run clean test-client stress-test