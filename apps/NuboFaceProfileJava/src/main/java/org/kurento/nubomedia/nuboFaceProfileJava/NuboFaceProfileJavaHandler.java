/*
 * (C) Copyright 2014 Kurento (http://kurento.org/)
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
package org.kurento.nubomedia.nuboFaceProfileJava;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import org.kurento.client.EventListener;
import org.kurento.client.IceCandidate;
import org.kurento.client.KurentoClient;
import org.kurento.client.MediaPipeline;
import org.kurento.client.OnIceCandidateEvent;
import org.kurento.client.Stats;
import org.kurento.client.WebRtcEndpoint;
import org.kurento.jsonrpc.JsonUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;
import org.kurento.module.nubofacedetector.*;
import org.kurento.module.nubomouthdetector.*;
import org.kurento.module.nubonosedetector.*;
import org.kurento.module.nuboeyedetector.*;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;

/**
 * Chroma handler (application and media logic).
 * 
 * @author Boni Garcia (bgarcia@gsyc.es)
 * @author David Fernandez (d.fernandezlop@gmail.com)
 * @since 6.0.0
 */
public class NuboFaceProfileJavaHandler extends TextWebSocketHandler {

	private final Logger log = LoggerFactory.getLogger(NuboFaceProfileJavaHandler.class);
	private static final Gson gson = new GsonBuilder().create();
	private final String EYE_FILTER = "eye";
	private final String MOUTH_FILTER = "mouth";
	private final String NOSE_FILTER = "nose";
	private final String FACE_FILTER = "face";

	private final ConcurrentHashMap<String, UserSession> users = new ConcurrentHashMap<String, UserSession>();

	@Autowired
	private KurentoClient kurento;
	private MediaPipeline pipeline = null;
	private WebRtcEndpoint webRtcEndpoint=null;
	private NuboFaceDetector face = null;
	private NuboMouthDetector mouth = null;
	private NuboNoseDetector nose = null;
	private NuboEyeDetector eye = null;
	
	private int visualizeFace = -1;
	private int visualizeMouth = -1;
	private int visualizeNose = -1;
	private int visualizeEye = -1;
	

	@Override
	public void handleTextMessage(WebSocketSession session, TextMessage message)
			throws Exception {
		JsonObject jsonMessage = gson.fromJson(message.getPayload(),
				JsonObject.class);

		log.debug("Incoming message: {}", jsonMessage);
		System.out.println(" ");
		System.out.println(" ");
		System.out.println(" ");
		switch (jsonMessage.get("id").getAsString()) {
		case "start":
			System.out.println("Start Received");
			start(session, jsonMessage);
			break;		
		case "show_faces":
			System.out.println("Show faces received");
			setViewFaces(session,jsonMessage);
			break;

		case "show_mouths":
			System.out.println("Show mouths received");
			setViewMouths(session,jsonMessage);
			break;
			
		case "show_noses":
			System.out.println("Show noses received");
			setViewNoses(session,jsonMessage);
			break;
			
		case "show_eyes":
			System.out.println("Show eyes received");	
			setViewEyes(session,jsonMessage);
			break;
		case "face_res":
			System.out.println("Face Resolution received");	
			changeResolution(FACE_FILTER,session,jsonMessage);
			System.out.println("Face Resolution received");
			break;
		case "mouth_res":
			System.out.println("Mouth resolution received");
			changeResolution(this.MOUTH_FILTER,session,jsonMessage);
			break;
		case "nose_res":
			System.out.println("Nose resolution received");
			changeResolution(this.NOSE_FILTER,session,jsonMessage);
			break;
		case "eye_res":
			System.out.println("Eye resolution received");
			changeResolution(this.EYE_FILTER,session,jsonMessage);
			break;
		case "fps":
			System.out.println("FPs resolution received");
			setFps(session,jsonMessage);
			break;
		case "scale_factor":
			System.out.println("Scale Factor received");
			setScaleFactor(session,jsonMessage);
			break;		
		case "activate_stats":
			activateStats(session);
			break;			
		case "get_stats":
			getStats(session);
			break;
		case "stop": {
			UserSession user = users.remove(session.getId());
			if (user != null) {
				user.release();
			}
			break;
		}
		case "onIceCandidate": {
			JsonObject candidate = jsonMessage.get("candidate")
					.getAsJsonObject();

			UserSession user = users.get(session.getId());
			if (user != null) {
				IceCandidate cand = new IceCandidate(candidate.get("candidate")
						.getAsString(), candidate.get("sdpMid").getAsString(),
						candidate.get("sdpMLineIndex").getAsInt());
				user.addCandidate(cand);
			}
			break;
		}

		default:
			System.out.println("Invalid message with id " + jsonMessage.get("id").getAsString());
			sendError(session,
					"Invalid message with id "
							+ jsonMessage.get("id").getAsString());
			break;
		}
	}

	private void start(final WebSocketSession session, JsonObject jsonMessage) {
		try {
			// Media Logic (Media Pipeline and Elements)
			UserSession user = new UserSession();
			pipeline = kurento.createMediaPipeline();
			user.setMediaPipeline(pipeline);
			webRtcEndpoint = new WebRtcEndpoint.Builder(pipeline)
					.build();
			user.setWebRtcEndpoint(webRtcEndpoint);
			users.put(session.getId(), user);

			webRtcEndpoint
					.addOnIceCandidateListener(new EventListener<OnIceCandidateEvent>() {

						@Override
						public void onEvent(OnIceCandidateEvent event) {
							JsonObject response = new JsonObject();
							response.addProperty("id", "iceCandidate");
							response.add("candidate", JsonUtils
									.toJsonObject(event.getCandidate()));
							try {
								synchronized (session) {
									session.sendMessage(new TextMessage(
											response.toString()));
								}
							} catch (IOException e) {
								log.debug(e.getMessage());
							}
						}
					});

			pipeline.setLatencyStats(true); 			
			face = new NuboFaceDetector.Builder(pipeline).build();
			face.sendMetaData(1);
			face.detectByEvent(0);
			face.showFaces(0);
			

			mouth = new NuboMouthDetector.Builder(pipeline).build();
			mouth.sendMetaData(0);
			mouth.detectByEvent(1);
			mouth.showMouths(0);

			nose = new NuboNoseDetector.Builder(pipeline).build();
			nose.sendMetaData(0);
			nose.detectByEvent(1);
			nose.showNoses(0);
			
			eye = new NuboEyeDetector.Builder(pipeline).build();
			eye.sendMetaData(0);
			eye.detectByEvent(1);
			eye.showEyes(0);
			
			webRtcEndpoint.connect(face);
			 	
			face.connect(mouth);			
			mouth.connect(nose);
			nose.connect(eye);			
			eye.connect(webRtcEndpoint);
			
			// SDP negotiation (offer and answer)
			String sdpOffer = jsonMessage.get("sdpOffer").getAsString();			
			String sdpAnswer = webRtcEndpoint.processOffer(sdpOffer);

			// Sending response back to client
			JsonObject response = new JsonObject();
			response.addProperty("id", "startResponse");
			response.addProperty("sdpAnswer", sdpAnswer);

			synchronized (session) {
				session.sendMessage(new TextMessage(response.toString()));
			}
			webRtcEndpoint.gatherCandidates();

		} catch (Throwable t) {
			sendError(session, t.getMessage());
		}
	}

	private void setViewFaces(WebSocketSession session,JsonObject jsonObject)
	{
		
		try{
			visualizeFace = jsonObject.get("val").getAsInt();
				
			if (null != face)
				face.showFaces(visualizeFace);
			
		} catch (Throwable t){
			sendError(session,t.getMessage());
		}
	}
	
	private void setViewMouths(WebSocketSession session,JsonObject jsonObject)
	{
		
		try{
			visualizeMouth = jsonObject.get("val").getAsInt();
				
			if (null != mouth)
				mouth.showMouths(visualizeMouth);
			
		} catch (Throwable t){
			sendError(session,t.getMessage());
		}
	}
	
	private void setViewNoses(WebSocketSession session,JsonObject jsonObject)
	{
		
		try{
			visualizeNose = jsonObject.get("val").getAsInt();
				
			if (null != nose)
				nose.showNoses(visualizeNose);
			
		} catch (Throwable t){
			sendError(session,t.getMessage());
		}
	}
	
	private void setViewEyes(WebSocketSession session,JsonObject jsonObject)
	{
		
		try{
			visualizeEye = jsonObject.get("val").getAsInt();
				
			if (null != eye)
				eye.showEyes(visualizeEye);
			
		} catch (Throwable t){
			sendError(session,t.getMessage());
		}
	}
	
	private void changeResolution(final String filterType, WebSocketSession session, JsonObject jsonObject)
	{
		int val;
		try{
			System.out.println("In change resolution");
			val = jsonObject.get("val").getAsInt();
			System.out.println("in Change Resolution filter type " + filterType + " value " );
			
			if (filterType == this.EYE_FILTER && null != eye)
				eye.widthToProcess(val);
			
			else if (filterType == this.MOUTH_FILTER && null != mouth)
				mouth.widthToProcess(val);
			else if (filterType == this.NOSE_FILTER && null != nose )
				nose.widthToProcess(val);
			else if (filterType == this.FACE_FILTER && null != face )
				face.widthToProcess(val);
			else if (filterType == this.EYE_FILTER && null != eye )
				eye.widthToProcess(val);
			
		} catch (Throwable t){
			System.out.println("Error Change Resolution ");
			sendError(session,t.getMessage());
		}
	}

	private void setFps(WebSocketSession session,JsonObject jsonObject)
	{
		int val;
		try{
			val = jsonObject.get("val").getAsInt();
				
			if (null != face)
				face.processXevery4Frames(val);
			if (null != nose)
				nose.processXevery4Frames(val);
			if ( null != mouth)
				mouth.processXevery4Frames(val);
			if ( null != eye)
				eye.processXevery4Frames(val);
			
		} catch (Throwable t){
			sendError(session,t.getMessage());
		}
	}
	
	private void setScaleFactor (WebSocketSession session,JsonObject jsonObject)
	{
		int val;
		try{
			val = jsonObject.get("val").getAsInt();
				
			if (null != face)
				face.multiScaleFactor(val);				
			
		} catch (Throwable t){
			sendError(session,t.getMessage());
		}
	}
	
	private void activateStats(WebSocketSession session)
    {
    	try {
    		pipeline.setLatencyStats(true);    		
    		
    	} catch (Throwable t){
    		sendError(session,t.getMessage());
    		
    	}
    }
    
    private void getStats(WebSocketSession session)
    {
    	Map<String,Stats> face_stats;
    	Map<String,Stats> mouth_stats;
    	Map<String,Stats> nose_stats;
    	Map<String,Stats> eye_stats;
    	Map<String,Stats> wr_stats;
    	    
    	try{
    		face_stats  = face.getStats();
    		mouth_stats = mouth.getStats();
    		nose_stats  = nose.getStats();
    		eye_stats   = eye.getStats();
    		wr_stats   = webRtcEndpoint.getStats();
    		/***********************+getStats("Video", callback);*********************************/
    		Map.Entry<String,Stats>f_st = face_stats.entrySet().iterator().next();
    		Map.Entry<String,Stats>m_st = mouth_stats.entrySet().iterator().next();
    		Map.Entry<String,Stats>n_st = nose_stats.entrySet().iterator().next();
    		Map.Entry<String,Stats>e_st = eye_stats.entrySet().iterator().next();
    		Map.Entry<String,Stats>wr_st = wr_stats.entrySet().iterator().next();
    		
    		double f_time= f_st.getValue().getTimestamp() - m_st.getValue().getTimestamp();
    		double m_time= m_st.getValue().getTimestamp() - n_st.getValue().getTimestamp();
    		double n_time= n_st.getValue().getTimestamp() - e_st.getValue().getTimestamp();
    		double e_time= e_st.getValue().getTimestamp() - wr_st.getValue().getTimestamp();
    		
    		
    		
    	}catch (Throwable t){
    		sendError(session,t.getMessage());
    		
    	}
    }
	private void sendError(WebSocketSession session, String message) {
		try {
			JsonObject response = new JsonObject();
			response.addProperty("id", "error");
			response.addProperty("message", message);
			session.sendMessage(new TextMessage(response.toString()));
		} catch (IOException e) {
			log.error("Exception sending message", e);
		}
	}
}
