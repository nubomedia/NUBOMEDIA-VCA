package com.visual_tools.nubomedia.nuboNoseJava;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

/**
 * Chroma main class.
 * 
 * @author Victor Hidalgo (vmhidalgo@gmail.com)
 * @since 5.0.0
 */

@SpringBootApplication
@EnableWebSocket
public class NuboNoseJavaApp implements WebSocketConfigurer {

    //final static String DEFAULT_KMS_WS_URI = "ws://localhost:8888/kurento";	

	@Bean
	public NuboNoseJavaHandler handler() {
		return new NuboNoseJavaHandler();
	}

	@Override
	public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
		registry.addHandler(handler(), "/nubonosedetector");
	}

	public static void main(String[] args) throws Exception {
		new SpringApplication(NuboNoseJavaApp.class).run(args);
	}
}
