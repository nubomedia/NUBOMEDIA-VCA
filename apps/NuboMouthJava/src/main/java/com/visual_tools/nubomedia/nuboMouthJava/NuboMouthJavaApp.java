package com.visual_tools.nubomedia.nuboMouthJava;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

/**
 * Mouth main class.
 * 
 * @author Victor Hidalgo  (vmhidalgo@visual-tools.com)
 * @since 5.0.0
 */

@SpringBootApplication
@EnableWebSocket

public class NuboMouthJavaApp implements WebSocketConfigurer {

    //final static String DEFAULT_KMS_WS_URI = "wss://localhost:8433/kurento";
    
    @Bean
    public NuboMouthJavaHandler handler() {
	return new NuboMouthJavaHandler();
    }
    
    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
	registry.addHandler(handler(), "/nubomouthdetector");
    }
    
    public static void main(String[] args) throws Exception {
	new SpringApplication(NuboMouthJavaApp.class).run(args);
    }
}
