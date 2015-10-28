package com.vtools.nubomedia.nuboTrackerJava;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import org.kurento.client.EventListener;
import org.kurento.client.IceCandidate;
import org.kurento.client.KurentoClient;
import org.kurento.client.MediaPipeline;
import org.kurento.client.OnIceCandidateEvent;
import org.kurento.client.WebRtcEndpoint;
import org.kurento.jsonrpc.JsonUtils;
import org.kurento.module.nubotracker.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;
import org.kurento.client.EndpointStats;
import org.kurento.client.Stats;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;

/**
 * Tracker  handler (application and media logic).
 * 
 * @author Victor Hidalgo  (vmhidalgo@visual-tools.com)
 * @since 6.0.0
 */
public class NuboTrackerJavaHandler extends TextWebSocketHandler {

	private final Logger log = LoggerFactory.getLogger(NuboTrackerJavaHandler.class);
	private static final Gson gson = new GsonBuilder().create();

	private final ConcurrentHashMap<String, UserSession> users = new ConcurrentHashMap<String, UserSession>();

	@Autowired
	private KurentoClient kurento;
	private MediaPipeline pipeline = null;
	private WebRtcEndpoint webRtcEndpoint = null;
	private NuboTracker tracker= null;	

	@Override
	public void handleTextMessage(WebSocketSession session, TextMessage message)
			throws Exception {
		JsonObject jsonMessage = gson.fromJson(message.getPayload(),
				JsonObject.class);

		log.debug("Incoming message: {}", jsonMessage);

		switch (jsonMessage.get("id").getAsString()) {
		case "start":
			start(session, jsonMessage);
			break;

		case "threshold":					
			setThreshold(session,jsonMessage);			
			break;
			
		case "min_area":
			setMinArea(session, jsonMessage);
			break;
			
		case "max_area":			
			setMaxArea(session, jsonMessage);			
			break;
		
		case "distance":				
			setDistance(session, jsonMessage);			
			break;
			
		case "visual_mode":
			setVisualMode(session,jsonMessage);
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
			pipeline.setLatencyStats(true);
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

			tracker = new NuboTracker.Builder(pipeline).build();

			
			webRtcEndpoint.connect(tracker);
			tracker.connect(webRtcEndpoint);
			
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


	private void setThreshold(WebSocketSession session, JsonObject jsonObject) {
					
		int threshold;
		try{
			
			threshold = jsonObject.get("val").getAsInt();
			
			if (null != tracker)
				tracker.setThreshold(threshold);
			
		}catch (Throwable t)
		{
			System.out.println("Threshold ERROR");
			sendError(session, t.getMessage());
		}
	}
	
	private void setMinArea(WebSocketSession session, JsonObject jsonObject) {
		
		try{			
			int min_area= jsonObject.get("val").getAsInt();			
			if (null != tracker)
			tracker.setMinArea(min_area);
			
		}catch (Throwable t)
		{
			System.out.println("Error......");
			sendError(session, t.getMessage());
		}
	}
	
	private void setMaxArea(WebSocketSession session, JsonObject jsonObject) {		
		try{			
			float max_area= jsonObject.get("val").getAsFloat();			
			if (null != tracker)
			tracker.setMaxArea(max_area);			
		}catch (Throwable t)
		{			
			sendError(session, t.getMessage());
		}
	}
	
	private void setDistance(WebSocketSession session, JsonObject jsonObject) {
		try{
		
			int distance= jsonObject.get("val").getAsInt();
			
			if (null != tracker)
				tracker.setDistance(distance);
			
		}catch (Throwable t)
		{			
			sendError(session, t.getMessage());
		}		
	}
	private void setVisualMode(WebSocketSession session, JsonObject jsonObject) {
		try{
			
			int mode= jsonObject.get("val").getAsInt();
			
			if (null != tracker)
				tracker.setVisualMode(mode);				
				
		}catch (Throwable t)
		{			
			sendError(session, t.getMessage());
		}
	}
	

    private void getStats(WebSocketSession session)
    {    	
    	try {
    		Map<String,Stats> wr_stats= webRtcEndpoint.getStats();
    	
    		for (Stats s :  wr_stats.values()) {
    		
    			switch (s.getType()) {		
    			case endpoint:
    				EndpointStats end_stats= (EndpointStats) s;
    				double e2eVideLatency= end_stats.getVideoE2ELatency() / 1000000;
    				
    				JsonObject response = new JsonObject();
    				response.addProperty("id", "videoE2Elatency");
    				response.addProperty("message", e2eVideLatency);				
    				session.sendMessage(new TextMessage(response.toString()));				
    				break;
	
    			default:	
    				break;
    			}				
    		}
    	} catch (IOException e) {
			log.error("Exception sending message", e);
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
