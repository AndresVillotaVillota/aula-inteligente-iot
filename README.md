# Aula Inteligente — Capa de Percepción

**Universidad de Antioquia**  
**Asignatura:** Internet de las Cosas (IoT)  
**Proyecto:** Aula Inteligente  
**Entregable 1:** Implementación de la capa de percepción

## 1. Descripción del proyecto

Aula Inteligente es un proyecto de Internet de las Cosas (IoT) orientado al monitoreo de las condiciones ambientales y la presencia de personas en un espacio académico, utilizando sensores, actuadores y un microcontrolador ESP32.

En este primer entregable se desarrolla la capa de percepción, responsable de adquirir información del entorno y ejecutar acciones de control sobre dispositivos físicos.

La implementación utiliza ESP-IDF, FreeRTOS y PlatformIO.

## 2. Integrantes

| Nombre | Correo institucional |
|---|---|
| Andrés Alejandro Villota Villota | andres.villotav@udea.edu.co |
| Ricardo José Portnoy Tomás | ricardo.portnoy@udea.edu.co |

## 3. Componentes del proyecto

### Microcontrolador
- ESP32 DOIT DevKit V1.

### Sensores
- DHT22: temperatura y humedad.
- PIR: detección de movimiento o presencia.
- LM35: temperatura analógica.
- BH1750: intensidad luminosa mediante I2C.
- LDR: sensor de luz contemplado en el esquema eléctrico.

### Actuadores
- Ventilador DC de 5 V.
- LED rojo y LED verde.
- MOSFET de canal N para controlar el ventilador.
- Resistencias y diodo de protección.

## 4. Tecnologías utilizadas

- Lenguaje C.
- ESP-IDF.
- FreeRTOS.
- PlatformIO.
- Visual Studio Code.
- Git y GitHub.
- KiCad para los esquemas eléctricos.

## 5. Funcionamiento general

El sistema está diseñado para adquirir información de los sensores y utilizarla en decisiones de control sobre los actuadores.

El código fuente incluye una tarea denominada `Control_Task`, implementada con FreeRTOS, que ejecuta periódicamente la lógica de lectura y control.

La arquitectura permite continuar posteriormente con la integración de comunicaciones y servicios IoT.

**Nota:** La LDR aparece en el diseño eléctrico, pero su lectura no está implementada en el código fuente de este entregable.

## 6. Organización del repositorio

```text
aula-inteligente-iot/
├── evidencias/
│   ├── esquema_1.jpeg
│   ├── esquema_2.jpeg
│   ├── esquema_3.jpeg
│   ├── esquema_4.jpeg
│   ├── esquema_5.jpeg
│   ├── montaje_1.jpeg
│   ├── montaje_2.jpeg
│   └── montaje_3.jpeg
├── src/
│   ├── CMakeLists.txt
│   └── main.c
├── CMakeLists.txt
├── platformio.ini
└── README.md
```

## 7. Configuración del proyecto

La placa se configura en `platformio.ini`:

```ini
[env:esp32doit-devkit-v1]
platform = espressif32
board = esp32doit-devkit-v1
framework = espidf
monitor_speed = 115200
```

Para trabajar con el proyecto:

1. Instalar Visual Studio Code y la extensión PlatformIO.
2. Clonar o descargar este repositorio.
3. Abrir la carpeta del proyecto en PlatformIO.
4. Verificar las conexiones del hardware antes de energizarlo.
5. Compilar y cargar el firmware en la placa ESP32 correspondiente.
6. Consultar el monitor serial a 115200 baudios.

## 8. Evidencias

El directorio [`evidencias/`](evidencias/) contiene fotografías del montaje físico y diagramas de conexiones utilizados para documentar el desarrollo de la capa de percepción.

Estas evidencias permiten identificar el microcontrolador, los componentes y la organización de los circuitos.

## 9. Video demostrativo

El video de presentación y demostración del entregable está disponible en YouTube:

**[Ver video del proyecto Aula Inteligente](https://youtu.be/uLynNbz5UMI)**

## 10. Estado del proyecto

Este repositorio corresponde al primer entregable de la asignatura, centrado en la capa de percepción.

El proyecto continuará evolucionando mediante la integración de los componentes y las funcionalidades correspondientes a las siguientes etapas.

## 11. Uso de inteligencia artificial

Se utilizó ChatGPT como herramienta de apoyo para la organización de la documentación técnica, revisión del informe, la estructuración del repositorio y la revisión de aspectos del desarrollo.

---

**Universidad de Antioquia — Internet de las Cosas (IoT)**  
**Proyecto académico: Aula Inteligente**
