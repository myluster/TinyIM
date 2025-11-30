# TinyIM 手动测试指南

## 快速测试步骤

### 1. 清理环境
```cmd
cd d:\QtProjection\TinyIM
infra\scripts\stop_dev.bat
```

### 2. Single-Node 环境测试

**启动环境**:
```cmd
infra\scripts\start_single.bat
```
等待 15 秒。

**启动后端服务**:
```cmd
infra\scripts\run_backend_single.bat
```

**运行测试**:
```cmd
docker-compose -f infra\compose\docker-compose-single.yml exec -T tinyim-dev bash -c "cd /app/build/tests && ./test_features ../../configs/config.json"
```

**预期输出**: `All Feature Tests Passed!`

### 3. HA 环境测试

**停止 Single-Node**:
```cmd
infra\scripts\stop_dev.bat
```
等待 10 秒。

**启动 HA 环境**:
```cmd
infra\scripts\start_ha.bat
```
等待 60 秒（MySQL 复制配置需要时间）。

**启动后端服务**:
```cmd
infra\scripts\run_backend_ha.bat
```

**运行测试**:
```cmd
docker-compose -f infra\compose\docker-compose-ha.yml exec -T tinyim-dev bash -c "cd /app/build/tests && ./test_features ../../configs/config.json"
```

**预期输出**: `All Feature Tests Passed!`

## 故障排查

**检查容器状态**:
```cmd
docker ps --filter "name=tinyim"
```

**查看日志**:
```cmd
:: Single-Node
docker-compose -f infra\compose\docker-compose-single.yml logs tinyim-dev

:: HA
docker-compose -f infra\compose\docker-compose-ha.yml logs tinyim-dev
```

**检查 MySQL (HA)**:
```cmd
docker exec tinyim_mysql_master mysql -uroot -proot_password -e "SHOW DATABASES;"
docker exec tinyim_mysql_slave_1 mysql -uroot -proot_password -e "SHOW SLAVE STATUS\G" | findstr "Running"
```
