package com.visual_tools.nubomedia.nuboFaceJava;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

/**
 * Face main class.
 * 
 * @author Victor Hidalgo (vmhidalgo@visual-tools.com)
 * @since 6.0.0
 */

@SpringBootApplication
@EnableWebSocket

public class NuboFaceJavaApp implements WebSocketConfigurer {

    //final static String DEFAULT_KMS_WS_URI = "wss://localhost:8888/kurento";
	
	@Bean
	public NuboFaceJavaHandler handler() {
		return new NuboFaceJavaHandler();
	}
       
	@Override
	public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
		registry.addHandler(handler(), "/nubofacedetector");
	}

	public static void main(String[] args) throws Exception {
		new SpringApplication(NuboFaceJavaApp.class).run(args);
	}
}
