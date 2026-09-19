#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "applicationInternal/storage/configScenes.h"

/*
  Runs a list of commands with pauses in between, without blocking.

  Today the scenes do this with delay(): scene_allOff() holds the main loop for
  3.5 seconds while it switches seven devices off. During that time the display
  does not redraw, a key press is not seen and the IR receiver is deaf. It is
  survivable for a scene, but step 24 turns this into macros that users write
  themselves - and a macro with a two minute pause in it must not freeze the
  remote.

  So a sequence becomes data: a list of {command, payload, delayAfter} that the
  main loop steps through. That is the same structure scenes.json already
  stores, which is why step 6 defined it before there was an engine to run it.

  ## One queue, and why

  Switching scenes calls the end sequence of the old scene and then the start
  sequence of the new one, back to back (see sceneHandler.cpp). With delay()
  those ran one after the other; the engine has to keep that, so enqueue()
  appends rather than replaces.

  A *new* scene switch is different: whatever is still pending belongs to a
  scene the user has moved on from. sceneHandler calls abort() first, so the
  pending steps are dropped and the two new sequences take over.
*/

namespace sequenceEngine {

/*
  A real constructor rather than default member initialisers: the scenes write
  their sequences as a braced list, and with default member initialisers Step
  would not be an aggregate under the standard the Arduino core compiles with.
  The unit tests build with gnu++17, where it would have worked - so this is
  exactly the kind of difference that passes the tests and breaks the firmware.
*/
struct Step {
  uint16_t command;
  std::string payload;  // passed as additionalPayload
  uint32_t delayAfterMs; // wait before the next step

  Step(uint16_t aCommand = 0, const std::string &aPayload = "", uint32_t aDelayAfterMs = 0)
      : command(aCommand), payload(aPayload), delayAfterMs(aDelayAfterMs) {}
};

// Appends to the queue. Steps already pending keep their place.
void enqueue(const std::vector<Step> &steps);

/*
  Same, but from the configuration format, where commands are referenced by
  name. A name that is not registered is skipped with a warning and named in
  error - one unknown command must not silently swallow the rest of the scene.
  Returns false if at least one step was dropped.
*/
bool enqueueFromConfig(const std::vector<configModel::SequenceStep> &steps, std::string &error);

// Drops everything pending. Used when the user moves on to another scene.
void abort();

/*
  Call from the main loop. currentMillis is passed in rather than read, for the
  same reason getKeys() takes it: it makes the whole thing testable without
  sleeping, and the simulator has no millis() of its own.
*/
void loop(unsigned long currentMillis);

bool isRunning();
size_t pendingSteps();

/*
  Hands over the pending steps and empties the queue, without running any of
  them.

  This is how a scene gets exported. The start and end sequences are still C++
  functions, but all they do now is enqueue - so calling one with an empty queue
  and taking the result afterwards yields the sequence as data, without sending
  a single IR command at the TV in the living room.
*/
std::vector<Step> takePending();

} // namespace sequenceEngine
