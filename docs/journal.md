# Журнал виконання ЛР1

## Проблема 1: CubeMX не ставиться, хоча Java є
Симптом: інсталятор просить JRE 17+, хоча `java -version` = 21.
Фікс: `ln -s /usr/lib/jvm/java-17-openjdk-amd64 ~/Загрузки/jre`
