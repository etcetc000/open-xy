#include <opxy/arranger.hpp>

extern "C" int puts(const char*);
extern "C" void exit(int);

using namespace opxy;
using namespace opxy::arranger;

void require(bool condition, const char* message) {
    if (!condition) { puts(message); exit(1); }
}

int main() {
    {
        Model edit;
        require(edit.insert({0, 2, 100, 300, 80, 96}) == 0, "split setup");
        require(edit.split(0, 100) < 0 && edit.split(0, 400) < 0, "split edges rejected");
        require(edit.split(0, 230) == 1, "split allocates right");
        require(edit.clip(0)->length == 130 && edit.clip(1)->offset == 18 &&
            edit.clip(1)->length == 170 && edit.clip(1)->pattern == 2, "split phase wraps");
        require(!edit.trimLeft(1, 229) && edit.clip(1)->start == 230, "left extension overlap atomic");
        require(edit.trimLeft(1, 250) && edit.clip(1)->offset == 38 && edit.clip(1)->length == 150, "left trim phase");
        require(edit.trimLeft(1, 230) && edit.clip(1)->offset == 18, "left extension restores phase");
        require(!edit.trimLeft(1, 400), "zero left trim rejected");
        require(edit.slip(1, -20) && edit.clip(1)->offset == 94, "negative slip wrap");
        require(edit.slip(1, (-2147483647 - 1)) && edit.clip(1)->offset == 62, "minimum signed slip");
        require(edit.slip(1, 2147483647) && edit.clip(1)->offset == 93, "maximum signed slip");
        Clip changed = *edit.clip(1); changed.start = 100;
        require(!edit.replace(1, changed) && edit.clip(1)->start == 230, "replace overlap atomic");
        Model full;
        for (u32 i = 0; i < Model::capacity; ++i)
            require(full.insert({0, 0, i * 2, 2, 0, 1}) >= 0, "fill split model");
        require(full.split(0, 1) < 0 && full.clip(0)->length == 2, "full split rollback");
        Model wide;
        require(wide.insert({0, 0, 0, Model::max_time, Model::max_time - 1, Model::max_time}) == 0, "wide phase setup");
        require(wide.split(0, 2) == 1 && wide.clip(1)->offset == 1, "split phase no overflow");
    }

    Model model;
    const Clip source = {0, 3, 100, 210, 24, 96};
    const i32 first = model.insert(source);
    require(first == 0, "first slot");
    u32 phase = 99;
    require(!model.active(0, 99, phase) && phase == 0, "before clip");
    require(model.active(0, 100, phase) && phase == 24, "offset at start");
    require(model.active(0, 200, phase) && phase == 28, "seek into repeated source");
    require(model.active(0, 309, phase) && phase == 41, "last included tick");
    require(!model.active(0, 310, phase), "exclusive end");
    require(!model.active(1, 200, phase), "track filter");
    const i32 copy = model.duplicate(first, 310);
    require(copy == 1 && model.clip(copy)->pattern == 3, "adjacent shared duplicate");
    require(model.duplicate(first, 309) == -1, "overlapping duplicate rejected");
    require(!model.resize(first, 211) && model.clip(first)->length == 210, "resize overlap atomic");
    require(!model.resize(first, 0) && model.clip(first)->length == 210, "zero resize atomic");
    require(!model.resize(first, Model::max_time), "resize overflow rejected");
    require(!model.resize(Model::capacity, 1), "resize missing slot");
    require(model.resize(first, 100) && model.clip(first)->offset == 24 &&
        model.clip(first)->source_length == 96 && model.clip(first)->pattern == 3,
        "trim preserves source identity and phase");
    require(!model.active(0, 200, phase), "trim end exclusive");
    require(model.resize(first, 210), "extend to adjacent boundary");
    require(!model.move(copy, 0, 309) && model.clip(copy)->start == 310, "failed move atomic");
    require(!model.move(copy, 8, 520) && model.clip(copy)->track == 0, "invalid track atomic");
    require(!model.move(copy, 0, Model::max_time), "overflow rejected");
    require(model.move(copy, 7, 101), "independent track move");
    require(model.remove(first) && !model.clip(first) && model.clip(copy)->start == 101, "stable remaining slot");
    require(!model.remove(first) && model.duplicate(first, 0) == -1, "missing slot");
    require(model.insert(source) == first, "free slot reuse");
    Clip invalid = source;
    invalid.track = 8; require(model.insert(invalid) == -1, "invalid track");
    invalid = source; invalid.pattern = 16; require(model.insert(invalid) == -1, "invalid pattern");
    invalid = source; invalid.source_length = 0; require(model.insert(invalid) == -1, "zero source");
    invalid = source; invalid.length = 0; require(model.insert(invalid) == -1, "zero clip");
    invalid = source; invalid.offset = 96; require(model.insert(invalid) == -1, "invalid offset");

    Model full;
    for (u32 i = 0; i < Model::capacity; ++i) {
        const Clip item = {0, 0, i, 1, 0, 1};
        require(full.insert(item) == static_cast<i32>(i), "fill slots");
    }
    const Clip extra = {0, 0, 256, 1, 0, 1};
    require(full.insert(extra) == -1 && full.clip(255)->start == 255, "full insertion atomic");
    require(full.duplicate(0, 256) == -1, "full duplicate atomic");

    model.seek(280);
    require(model.setLoop(true, 100, 300), "loop valid");
    require(model.advance(20) == 100, "exact loop boundary");
    model.seek(280); require(model.advance(445) == 125, "multi-loop overshoot");
    model.seek(320); require(model.advance(0) == 120, "seek beyond loop");
    model.seek(0); require(model.advance(50) == 50, "pre-loop intro");
    require(!model.setLoop(true, 300, 100), "invalid loop rejected");
    model.seek(280); require(model.advance(20) == 100, "failed loop update atomic");
    model.setLoop(false, 0, 1); model.seek(Model::max_time - 1);
    require(model.advance(3) == Model::max_time, "non-loop saturates");
    model.setLoop(true, 0, Model::max_time); model.seek(Model::max_time);
    require(model.advance(Model::max_time - 1) == Model::max_time - 1, "large modular step");
    model.setLoop(true, 100, Model::max_time); model.seek(Model::max_time);
    require(model.advance(Model::max_time) == 200, "large loop overflow avoided");
    Model large;
    const Clip wide = {0, 15, 0, Model::max_time, Model::max_time - 1, Model::max_time};
    require(large.insert(wide) == 0, "large clip");
    require(large.active(0, Model::max_time - 1, phase) && phase == Model::max_time - 2, "source phase overflow avoided");

    HoldChord chord;
    chord.set(Key::Sequencer, true, 0); require(!chord.poll(1000), "one key does not fire");
    chord.set(Key::Sample, true, 1000); require(!chord.poll(1599), "hold threshold");
    chord.set(Key::Sample, true, 1500); require(chord.poll(1600), "repeats do not reset hold");
    require(!chord.poll(3000), "fires once");
    chord.set(Key::Sample, false, 3100); chord.set(Key::Sample, true, 3200);
    require(!chord.poll(4000), "both must release");
    chord.set(Key::Sample, false, 4100); chord.set(Key::Sequencer, false, 4100);
    chord.set(Key::Sample, true, 4200); chord.set(Key::Sequencer, true, 4201);
    require(chord.poll(4801), "rearm after both released");
    chord.reset(); chord.set(Key::Sample, true, 0); chord.set(Key::Sequencer, true, 0);
    chord.set(Key::Sample, false, 599); require(!chord.poll(1000), "interrupted hold");
    chord.set(Key::Sample, true, 1001); require(chord.poll(1601), "new continuous hold");
    chord.reset(); chord.set(Key::Sample, true, Model::max_time - 299);
    chord.set(Key::Sequencer, true, Model::max_time - 299);
    require(!chord.poll(299) && chord.poll(300), "millisecond counter wrap");
    return 0;
}
