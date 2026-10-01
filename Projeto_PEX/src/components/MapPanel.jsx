import { useEffect, useRef, useState } from "react";
import * as Cesium from "cesium";
import "cesium/Build/Cesium/Widgets/widgets.css";
import { BASE_LAT, BASE_LNG, MOCK_ROUTE_PTS } from "../lib/dashboard";

// Camera altitude presets (meters). WIDE keeps the original broad overview;
// CLOSE gives a much tighter view for following the drone between waypoints.
const WIDE_ALTITUDE = 1800;
const FOLLOW_ALTITUDE = 220;
// Allow Cesium to zoom in far closer than the default floor.
const MIN_ZOOM_DISTANCE = 15;
const MAX_ZOOM_DISTANCE = 20000000;
const CAMERA_PITCH = Cesium.Math.toRadians(-55);
// Follow mode looks straight down (nadir) so the drone stays visible and centered,
// instead of the oblique 3D tilt that hides it behind the horizon.
const FOLLOW_PITCH = Cesium.Math.toRadians(-90);

// Oriented boat arrow (points "up" at rotation 0 → screen north). White outline so
// it reads over any imagery; the tip marks the bow. Rendered via a Cesium billboard
// whose `rotation` is driven by the compass heading below.
const BOAT_ARROW_SVG = encodeURIComponent(
  `<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 48 48">` +
    `<path d="M24 3 L40 41 L24 32 L8 41 Z" fill="#2563eb" stroke="#ffffff" stroke-width="2.5" stroke-linejoin="round"/>` +
  `</svg>`
);
const BOAT_ARROW_IMG = `data:image/svg+xml;charset=UTF-8,${BOAT_ARROW_SVG}`;

// Compass heading (deg, north-up, clockwise) → Cesium billboard rotation (radians,
// counter-clockwise from screen-up). The SVG points up at rotation 0, so negate.
function headingToBillboardRotation(headingDeg) {
  return -Cesium.Math.toRadians(headingDeg ?? 0);
}

function pointPosition(point, height = 0) {
  return Cesium.Cartesian3.fromDegrees(point.lon, point.lat, height);
}

function addPoint(viewer, point, color, label, size = 10) {
  viewer.entities.add({
    position: pointPosition(point, 3),
    point: {
      pixelSize: size,
      color: Cesium.Color.fromCssColorString(color),
      outlineColor: Cesium.Color.WHITE,
      outlineWidth: 2,
      disableDepthTestDistance: Number.POSITIVE_INFINITY,
    },
    label: {
      text: label,
      font: "600 12px DM Sans, sans-serif",
      fillColor: Cesium.Color.WHITE,
      outlineColor: Cesium.Color.fromCssColorString("#0f172a"),
      outlineWidth: 3,
      style: Cesium.LabelStyle.FILL_AND_OUTLINE,
      verticalOrigin: Cesium.VerticalOrigin.BOTTOM,
      pixelOffset: new Cesium.Cartesian2(0, -size),
      disableDepthTestDistance: Number.POSITIVE_INFINITY,
    },
  });
}

export function MapPanel({ telemetry, mission, path, onMapClick, isMobile }) {
  const containerRef = useRef(null);
  const viewerRef = useRef(null);
  const onMapClickRef = useRef(onMapClick);
  const hasInitialViewRef = useRef(false);
  const [followDrone, setFollowDrone] = useState(false);
  const followDroneRef = useRef(followDrone);

  useEffect(() => {
    followDroneRef.current = followDrone;
  }, [followDrone]);

  useEffect(() => {
    onMapClickRef.current = onMapClick;
  }, [onMapClick]);

  useEffect(() => {
    if (!containerRef.current) {
      return undefined;
    }

    const viewer = new Cesium.Viewer(containerRef.current, {
      baseLayer: false,
      terrainProvider: new Cesium.EllipsoidTerrainProvider(),
      animation: false,
      timeline: false,
      geocoder: false,
      homeButton: false,
      sceneModePicker: false,
      navigationHelpButton: false,
      fullscreenButton: false,
      infoBox: false,
      selectionIndicator: false,
      skyBox: false,
      skyAtmosphere: false,
    });

    viewer.imageryLayers.addImageryProvider(
      new Cesium.OpenStreetMapImageryProvider({
        url: "https://tile.openstreetmap.org/",
      })
    );
    viewer.scene.globe.baseColor = Cesium.Color.fromCssColorString("#dbeafe");
    viewer.scene.backgroundColor = Cesium.Color.fromCssColorString("#0f172a");
    viewer.scene.screenSpaceCameraController.enableCollisionDetection = false;
    // Let the operator zoom in much closer to follow the drone, while keeping
    // a wide maximum so the broad overview is still reachable.
    viewer.scene.screenSpaceCameraController.minimumZoomDistance = MIN_ZOOM_DISTANCE;
    viewer.scene.screenSpaceCameraController.maximumZoomDistance = MAX_ZOOM_DISTANCE;

    const clickHandler = (movement) => {
      const ray = viewer.camera.getPickRay(movement.position);
      const cartesian = ray && viewer.scene.globe.pick(ray, viewer.scene);
      if (!cartesian) {
        return;
      }

      const cartographic = Cesium.Cartographic.fromCartesian(cartesian);
      onMapClickRef.current?.(
        Cesium.Math.toDegrees(cartographic.latitude),
        Cesium.Math.toDegrees(cartographic.longitude)
      );
    };

    viewer.screenSpaceEventHandler.setInputAction(clickHandler, Cesium.ScreenSpaceEventType.LEFT_CLICK);
    viewerRef.current = viewer;

    return () => {
      viewer.destroy();
      viewerRef.current = null;
    };
  }, []);

  useEffect(() => {
    const viewer = viewerRef.current;
    if (!viewer) {
      return;
    }

    const routePoints = mission?.route?.points?.length ? mission.route.points : MOCK_ROUTE_PTS;
    const pathPoints = path?.length ? path : [];
    const activeLeg = mission?.route?.active_leg ?? 0;
    const dronePosition = telemetry?.position || { lat: BASE_LAT, lon: BASE_LNG };

    viewer.entities.removeAll();

    if (routePoints.length > 1) {
      viewer.entities.add({
        polyline: {
          positions: routePoints.map((point) => pointPosition(point, 2)),
          width: 4,
          material: new Cesium.PolylineDashMaterialProperty({
            color: Cesium.Color.fromCssColorString("#60a5fa").withAlpha(0.85),
            dashLength: 16,
          }),
          clampToGround: true,
        },
      });
    }

    if (pathPoints.length > 1) {
      viewer.entities.add({
        polyline: {
          positions: pathPoints.map((point) => pointPosition(point, 4)),
          width: 5,
          material: Cesium.Color.fromCssColorString("#1d4ed8").withAlpha(0.95),
          clampToGround: true,
        },
      });
    }

    routePoints.forEach((point, index) => {
      const completed = index < activeLeg;
      const current = index === activeLeg;
      addPoint(viewer, point, completed ? "#10b981" : current ? "#2563eb" : "#94a3b8", `${index + 1}`, current ? 12 : 9);
    });

    if (mission?.target) {
      addPoint(viewer, mission.target, "#8b5cf6", "Destino", 13);
    }

    const droneHeading = telemetry?.position?.heading ?? 0;
    viewer.entities.add({
      name: "USV-AM",
      // Position at terrain height (0); CLAMP_TO_GROUND makes the marker sit on the
      // Cesium "floor" instead of floating (was hardcoded to 14 m above ground).
      position: pointPosition(dronePosition, 0),
      billboard: {
        image: BOAT_ARROW_IMG,
        width: 34,
        height: 34,
        // Rotate the arrow to match the compass heading (bow points where it's going).
        rotation: headingToBillboardRotation(droneHeading),
        alignedAxis: Cesium.Cartesian3.ZERO, // rotation is screen-relative, not world-relative
        heightReference: Cesium.HeightReference.CLAMP_TO_GROUND,
        disableDepthTestDistance: Number.POSITIVE_INFINITY,
        verticalOrigin: Cesium.VerticalOrigin.CENTER,
      },
      label: {
        text: `USV-AM ${Math.round(droneHeading)}°`,
        font: "700 13px DM Sans, sans-serif",
        fillColor: Cesium.Color.WHITE,
        outlineColor: Cesium.Color.fromCssColorString("#0f172a"),
        outlineWidth: 3,
        style: Cesium.LabelStyle.FILL_AND_OUTLINE,
        verticalOrigin: Cesium.VerticalOrigin.BOTTOM,
        pixelOffset: new Cesium.Cartesian2(0, -24),
        heightReference: Cesium.HeightReference.CLAMP_TO_GROUND,
        disableDepthTestDistance: Number.POSITIVE_INFINITY,
      },
    });

    if (!hasInitialViewRef.current) {
      viewer.camera.flyTo({
        destination: Cesium.Cartesian3.fromDegrees(dronePosition.lon, dronePosition.lat, WIDE_ALTITUDE),
        orientation: {
          heading: 0,
          pitch: CAMERA_PITCH,
          roll: 0,
        },
        duration: 0.6,
      });
      hasInitialViewRef.current = true;
    } else if (followDroneRef.current) {
      // Follow mode: keep the drone centered at a close altitude as it moves.
      viewer.camera.flyTo({
        destination: Cesium.Cartesian3.fromDegrees(dronePosition.lon, dronePosition.lat, FOLLOW_ALTITUDE),
        orientation: {
          heading: 0,
          pitch: FOLLOW_PITCH,
          roll: 0,
        },
        duration: 0.4,
      });
    }
  }, [mission, path, telemetry]);

  useEffect(() => {
    const viewer = viewerRef.current;
    if (!viewer || !hasInitialViewRef.current) {
      return;
    }
    const dronePosition = telemetry?.position || { lat: BASE_LAT, lon: BASE_LNG };
    viewer.camera.flyTo({
      destination: Cesium.Cartesian3.fromDegrees(
        dronePosition.lon,
        dronePosition.lat,
        followDrone ? FOLLOW_ALTITUDE : WIDE_ALTITUDE
      ),
      orientation: { heading: 0, pitch: followDrone ? FOLLOW_PITCH : CAMERA_PITCH, roll: 0 },
      duration: 0.6,
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [followDrone]);

  return (
    <div style={{ position: "relative", borderRadius: 14, overflow: "hidden", boxShadow: "0 12px 30px rgba(15, 23, 42, 0.18)" }}>
      <div ref={containerRef} style={{ width: "100%", height: isMobile ? "clamp(250px, 46vh, 340px)" : "clamp(360px, 62vh, 640px)" }} />
      <button
        type="button"
        onClick={() => setFollowDrone((v) => !v)}
        title={followDrone ? "Seguindo o veiculo — clique para visao ampla" : "Aproximar e seguir o veiculo"}
        style={{
          position: "absolute",
          top: isMobile ? 8 : 10,
          right: isMobile ? 8 : 10,
          background: followDrone ? "#2563eb" : "rgba(15,23,42,0.82)",
          color: "#fff",
          border: "none",
          borderRadius: 8,
          padding: isMobile ? "6px 9px" : "7px 12px",
          fontSize: isMobile ? 10 : 12,
          fontWeight: 600,
          cursor: "pointer",
          display: "flex",
          alignItems: "center",
          gap: 6,
          boxShadow: "0 4px 12px rgba(15,23,42,0.25)",
        }}
      >
        <span style={{ fontSize: isMobile ? 12 : 14 }}>{followDrone ? "🎯" : "🛰"}</span>
        {followDrone ? "Seguindo" : "Seguir veiculo"}
      </button>
      <div style={{ position: "absolute", bottom: isMobile ? 8 : 10, right: isMobile ? 8 : 10, background: "rgba(15,23,42,0.82)", color: "#e2e8f0", borderRadius: 8, padding: isMobile ? "5px 7px" : "7px 10px", fontSize: isMobile ? 9 : 10, display: "flex", gap: isMobile ? 8 : 12, flexWrap: "wrap", maxWidth: isMobile ? "72%" : "none" }}>
        {[["#2563eb", "Veiculo"], ["#60a5fa", "Rota"], ["#10b981", "Pontos"], ["#8b5cf6", "Destino"]].map(([color, label]) => (
          <span key={label} style={{ display: "flex", alignItems: "center", gap: 4 }}>
            <span style={{ width: 7, height: 7, borderRadius: "50%", background: color, display: "inline-block" }} /> {label}
          </span>
        ))}
      </div>
      <div style={{ position: "absolute", bottom: isMobile ? 8 : 10, left: isMobile ? 8 : 10, background: "rgba(255,255,255,0.9)", borderRadius: 6, padding: isMobile ? "3px 6px" : "4px 9px", fontSize: isMobile ? 9 : 10, color: "#334155", maxWidth: isMobile ? "42%" : "none" }}>
        Clique no globo para definir destino
      </div>
    </div>
  );
}
