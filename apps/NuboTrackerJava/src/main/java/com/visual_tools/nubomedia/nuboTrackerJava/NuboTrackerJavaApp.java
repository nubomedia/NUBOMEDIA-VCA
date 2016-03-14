package com.visual_tools.nubomedia.nuboTrackerJava;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

/**
 * Tracker main class.
 * 
 * @author Victor Hidalgo (vmhidalgo@visual-tools.com)
 * @since 5.0.0
 */

@SpringBootApplication
@EnableWebSocket

public class NuboTrackerJavaApp implements WebSocketConfigurer {

	final static String DEFAULT_KMS_WS_URI = "ws://localhost:8888/kurento";
	

	@Bean
	public NuboTrackerJavaHandler handler() {
		return new NuboTrackerJavaHandler();
	}

	@Override
	public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
		registry.addHandler(handler(), "/nubotracker");
	}

	public static void main(String[] args) throws Exception {
		new SpringApplication(NuboTrackerJavaApp.class).run(args);
	}
}
