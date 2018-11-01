#include <BulbStateUpdater.h>

BulbStateUpdater::BulbStateUpdater(Settings& settings, MqttClient& mqttClient, GroupStateStore& stateStore)
  : settings(settings),
    mqttClient(mqttClient),
    stateStore(stateStore),
    lastFlush(0),
    enabled(true)
{ }

void BulbStateUpdater::enable() {
  this->enabled = true;
}

void BulbStateUpdater::disable() {
  this->enabled = false;
}

void BulbStateUpdater::enqueueUpdate(BulbId bulbId, GroupState& groupState) {
  // If can flush immediately, do so (avoids lookup of group state later).
  if (bulbId.groupId == 0) {
    const MiLightRemoteConfig* remote = MiLightRemoteConfig::fromType(bulbId.deviceType);
    BulbId individualBulb(bulbId);

    for (size_t i = 1; i <= remote->numGroups; i++) {
      individualBulb.groupId = i;
      if (canFlush()) {
        flushGroup(individualBulb, groupState);
      } else {
        staleGroups.push(individualBulb);
      }
    }
  } else {
    if (canFlush()) {
      flushGroup(bulbId, groupState);
    } else {
      staleGroups.push(bulbId);
    }
  }
}

void BulbStateUpdater::loop() {
  while (canFlush() && staleGroups.size() > 0) {
    BulbId bulbId = staleGroups.shift();
    GroupState* groupState = stateStore.get(bulbId);

    if (groupState->isMqttDirty()) {
      flushGroup(bulbId, *groupState);
      groupState->clearMqttDirty();
    }
  }
}

inline void BulbStateUpdater::flushGroup(BulbId bulbId, GroupState& state) {
  char buffer[200];
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& message = jsonBuffer.createObject();
  state.applyState(message, bulbId, settings.groupStateFields, settings.numGroupStateFields);
  message.printTo(buffer);

  mqttClient.sendState(
    *MiLightRemoteConfig::fromType(bulbId.deviceType),
    bulbId.deviceId,
    bulbId.groupId,
    buffer
  );

  lastFlush = millis();
}

inline bool BulbStateUpdater::canFlush() const {
  return enabled && (millis() > (lastFlush + settings.mqttStateRateLimit));
}
