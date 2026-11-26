#pragma once
#include <memory>
#include <string>

// Private, one-turn direct OpenAI dictation. Never writes audio or Notes saves.
class NotesDictation {
 public:
  struct Snapshot { std::string text; const char* status; bool busy; };
  NotesDictation();
  ~NotesDictation();
  bool start();
  void stop();  // rail off now, drain captured PCM and commit once
  void cancel();  // rail off now, no further provider/result authority
  void close();  // cancel and drain owned tasks before destruction
  Snapshot snapshot() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl;
  const char* idleStatus = "Ready";
};
