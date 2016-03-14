
package com.vtools.nubomedia.nuboFaceProfileJava;

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
 * @since 6.0.0
 */
@SpringBootApplication
@EnableWebSocket
public class NuboFaceProfileJavaApp implements WebSocketConfigurer {

	final static String DEFAULT_KMS_WS_URI = "ws://localhost:8888/kurento";
	

	@Bean
	public NuboFaceProfileJavaHandler handler() {
		return new NuboFaceProfileJavaHandler();
	}

	@Override
	public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
		registry.addHandler(handler(), "/nubofaceprofiledetector");
	}

	public static void main(String[] args) throws Exception {
		new SpringApplication(NuboFaceProfileJavaApp.class).run(args);
	}
}
