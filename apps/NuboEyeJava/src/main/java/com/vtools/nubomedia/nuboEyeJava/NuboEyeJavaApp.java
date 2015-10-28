
package com.vtools.nubomedia.nuboEyeJava;

import org.kurento.client.KurentoClient;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

/**
 * Chroma main class.
 * 
 * @author Victor Hidalgo (vmhidalgo@visual-tools.com)
 * @since 5.0.0
 */
@Configuration
@EnableWebSocket
@EnableAutoConfiguration
public class NuboEyeJavaApp implements WebSocketConfigurer {

	final static String DEFAULT_KMS_WS_URI = "ws://localhost:8888/kurento";
	
	@Bean
	public NuboEyeJavaHandler handler() {
		return new NuboEyeJavaHandler();
	}

	@Bean
	public KurentoClient kurentoClient() {
		return KurentoClient.create(System.getProperty("kms.ws.uri",
				DEFAULT_KMS_WS_URI));
	}

	@Override
	public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
		registry.addHandler(handler(), "/nuboeyedetector");
	}

	public static void main(String[] args) throws Exception {
		new SpringApplication(NuboEyeJavaApp.class).run(args);
	}
}
