
package com.visual_tools.nubomedia.nuboEyeJava;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

/**
 * Chroma main class.
 * 
 * @author Victor Hidalgo (vmhidalgo@visual-tools.com)
 * @since 5.0.0
 */
@SpringBootApplication
@EnableWebSocket
public class NuboEyeJavaApp implements WebSocketConfigurer {


	
	@Bean
	public NuboEyeJavaHandler handler() {
		return new NuboEyeJavaHandler();
	}

	@Override
	public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
		registry.addHandler(handler(), "/nuboeyedetector");
	}

	public static void main(String[] args) throws Exception {
		new SpringApplication(NuboEyeJavaApp.class).run(args);
	}
}
