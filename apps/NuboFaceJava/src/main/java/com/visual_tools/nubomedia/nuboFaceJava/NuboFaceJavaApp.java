/*
 * (C) Copyright 2015 Visual Tools (http://www.visual-tools.com/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 */

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

	final static String DEFAULT_KMS_WS_URI = "ws://localhost:8888/kurento";
	
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
