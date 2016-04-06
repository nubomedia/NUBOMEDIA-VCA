package com.vtools.nubomedia.nuboFaceProfileJava;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import org.kurento.client.EventListener;
import org.kurento.client.KurentoClient;
import org.kurento.client.OnIceCandidateEvent; 
import org.kurento.client.WebRtcEndpoint;
import org.kurento.jsonrpc.JsonUtils;
import org.kurento.client.internal.NotEnoughResourcesException; 
import org.kurento.module.nubomouthdetector.*;
import org.slf4j.Logger; 
import org.slf4j.LoggerFactory; 
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession; 
import org.springframework.web.socket.handler.TextWebSocketHandler; 
import org.kurento.client.EndpointStats; 
import org.kurento.client.Stats; 
import org.springframework.web.socket.CloseStatus; 

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
 * @author Victor Hidalgo (vmhidalgo@visual-tools.com)
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
		
		
	switch (jsonMessage.get("id").getAsString()) {
	case "start":		
	    start(session, jsonMessage);
	    break;	
			
	case "show_faces":		
	    setViewFaces(session,jsonMessage);
	    break;

	case "show_mouths":		
	    setViewMouths(session,jsonMessage);
	    break;
			
	case "show_noses":
	    setViewNoses(session,jsonMessage);
	    break;
			
	case "show_eyes":			
	    setViewEyes(session,jsonMessage);
	    break;

	case "face_res":
	    changeResolution(FACE_FILTER,session,jsonMessage);
	    break;
			
	case "mouth_res":
	    changeResolution(this.MOUTH_FILTER,session,jsonMessage);
	    break;
			
	case "nose_res":			
	    changeResolution(this.NOSE_FILTER,session,jsonMessage);
	    break;
			
	case "eye_res":			
	    changeResolution(this.EYE_FILTER,session,jsonMessage);
	    break;
			
	case "fps":			
	    setFps(session,jsonMessage);
	    break;
			
	case "scale_factor":			
	    setScaleFactor(session,jsonMessage);
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
	    JsonObject candidate = jsonMessage.get("candidate").getAsJsonObject();
	    
	    UserSession user = users.get(session.getId());
	    if (user != null) {
		user.addCandidate(candidate);
	    }
	    break;
	}

	default:
	    error(session,"Invalid message with id " + jsonMessage.get("id").getAsString());
	    break;
	}
    }

    private void start(final WebSocketSession session, JsonObject jsonMessage) {
	try {
	
	    String sessionId = session.getId();
	    UserSession user = new UserSession(sessionId);
	    users.put(sessionId,user);
	    webRtcEndpoint = user.getWebRtcEndpoint();
	    
	    //Ice Candidate
	    webRtcEndpoint.addOnIceCandidateListener(new EventListener<OnIceCandidateEvent>() {
		    @Override
			public void onEvent(OnIceCandidateEvent event) {
			JsonObject response = new JsonObject();
			response.addProperty("id", "iceCandidate");
			response.add("candidate", JsonUtils.toJsonObject(event.getCandidate()));
			sendMessage(session, new TextMessage(response.toString()));
		    }
		});

	    /******** Media Logic ********/
	    face = new NuboFaceDetector.Builder(user.getMediaPipeline()).build();
	    face.sendMetaData(1);
	    face.detectByEvent(0);	  
	    face.showFaces(0);
	    
	    mouth = new NuboMouthDetector.Builder(user.getMediaPipeline()).build();
	    mouth.sendMetaData(1);
	    mouth.detectByEvent(1);
	    mouth.showMouths(0);

	    nose = new NuboNoseDetector.Builder(user.getMediaPipeline()).build();
	    nose.sendMetaData(1);
	    nose.detectByEvent(1);
	    nose.showNoses(0);
			
	    eye = new NuboEyeDetector.Builder(user.getMediaPipeline()).build();
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
		sendMessage(session,new TextMessage(response.toString()));
	    }
	    webRtcEndpoint.gatherCandidates();
	    
	} catch (NotEnoughResourcesException e) {
	    log.warn("Not enough resources", e);
	    notEnoughResources(session);	    
	}
	catch (Throwable t) {
	    log.error("Exception starting session", t);
	    error(session, t.getClass().getSimpleName() + ": " + t.getMessage());
	}
    }

    private void notEnoughResources(WebSocketSession session) {
	// 1. Send notEnoughResources message to client
	JsonObject response = new JsonObject();
	response.addProperty("id", "notEnoughResources");
	sendMessage(session, new TextMessage(response.toString()));
	
	// 2. Release media session
	release(session);
    } 

    private void setViewFaces(WebSocketSession session,JsonObject jsonObject)
    {
		
	try{
	    visualizeFace = jsonObject.get("val").getAsInt();
				
	    if (null != face)
		face.showFaces(visualizeFace);
			
	} catch (Throwable t){
	    error(session, t.getClass().getSimpleName() + ": " + t.getMessage());
	}
    }
	
    private void setViewMouths(WebSocketSession session,JsonObject jsonObject)
    {
		
	try{
	    visualizeMouth = jsonObject.get("val").getAsInt();
				
	    if (null != mouth)
		mouth.showMouths(visualizeMouth);
			
	} catch (Throwable t){
	    error(session, t.getClass().getSimpleName() + ": " + t.getMessage());
	}
    }
	
    private void setViewNoses(WebSocketSession session,JsonObject jsonObject)
    {
		
	try{
	    visualizeNose = jsonObject.get("val").getAsInt();
				
	    if (null != nose)
		nose.showNoses(visualizeNose);
			
	} catch (Throwable t){
	    error(session, t.getClass().getSimpleName() + ": " + t.getMessage());
	}
    }
	
    private void setViewEyes(WebSocketSession session,JsonObject jsonObject)
    {
		
	try{
	    visualizeEye = jsonObject.get("val").getAsInt();
				
	    if (null != eye)
		eye.showEyes(visualizeEye);
			
	} catch (Throwable t){
	    error(session, t.getClass().getSimpleName() + ": " + t.getMessage());
	}
    }
	
    private void changeResolution(final String filterType, WebSocketSession session, JsonObject jsonObject)
    {
	int val;
	try{
			
	    val = jsonObject.get("val").getAsInt();
						
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
	    error(session, t.getClass().getSimpleName() + ": " + t.getMessage());
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
	    error(session, t.getClass().getSimpleName() + ": " + t.getMessage());
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
	    error(session, t.getClass().getSimpleName() + ": " + t.getMessage());
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
		    sendMessage(session,new TextMessage(response.toString()));				
		    break;	
		default:	
		    break;
		}				
	    }
    	} catch (Throwable t) {
	    log.error("Exception getting stats...", t);
	}
    	
    }
    
    
    private synchronized void sendMessage(WebSocketSession session, TextMessage message) {
	try {
	    session.sendMessage(message);
	} catch (IOException e) {	    log.error("Exception sending message", e);
	}
    }
    
    private void error(WebSocketSession session, String message) {

	JsonObject response = new JsonObject();
	response.addProperty("id", "error");
	response.addProperty("message", message);
	sendMessage(session,new TextMessage(response.toString()));
	// 2. Release media session
	release(session);
    }
    
    private void release(WebSocketSession session) {
	UserSession user = users.remove(session.getId());
	if (user != null) {
	    user.release();
	}
    }
    
    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) throws Exception {
	log.info("Closed websocket connection of session {}", session.getId());
	release(session);
    }
}
