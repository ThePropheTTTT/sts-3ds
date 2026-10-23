// Evaluate a Spine skeleton once and dump each bone's world matrix.
// Usage: jjs -cp desktop-1.0.jar -Dspine_skeleton=<skeleton.json> -Dspine_out=<out.json> [-Dspine_anim=Idle] tools/spine_bones.js
// Note: jjs on Java 8 mishandles dots in -D property names, hence the underscores.
var Files = java.nio.file.Files;
var Paths = java.nio.file.Paths;
var AttachmentLoader = com.esotericsoftware.spine.attachments.AttachmentLoader;
var RegionAttachment = com.esotericsoftware.spine.attachments.RegionAttachment;
var MeshAttachment = com.esotericsoftware.spine.attachments.MeshAttachment;
var BoundingBoxAttachment = com.esotericsoftware.spine.attachments.BoundingBoxAttachment;
var PathAttachment = com.esotericsoftware.spine.attachments.PathAttachment;

var Loader = Java.extend(AttachmentLoader, {
    newRegionAttachment: function (skin, name, path) { return new RegionAttachment(name); },
    newMeshAttachment: function (skin, name, path) { return new MeshAttachment(name); },
    newBoundingBoxAttachment: function (skin, name) { return new BoundingBoxAttachment(name); },
    newPathAttachment: function (skin, name) { return new PathAttachment(name); }
});

var skeletonPath = java.lang.System.getProperty("spine_skeleton");
var outPath = java.lang.System.getProperty("spine_out");
var animName = java.lang.System.getProperty("spine_anim");
var animTime = parseFloat(java.lang.System.getProperty("spine_anim_time", "0"));
var json = new com.esotericsoftware.spine.SkeletonJson(new Loader());
var file = new com.badlogic.gdx.files.FileHandle(skeletonPath);
var data = json.readSkeletonData(file);
var skeleton = new com.esotericsoftware.spine.Skeleton(data);
skeleton.setToSetupPose();
if (animName != null && animName.length() > 0) {
    var anim = data.findAnimation(animName);
    if (anim == null) { print("missing animation " + animName); quit(2); }
    anim.apply(skeleton, Math.max(0, animTime - 0.01), animTime, true, new com.badlogic.gdx.utils.Array());
}
skeleton.updateWorldTransform();
var bones = skeleton.getBones();
var out = [];
for (var i = 0; i < bones.size; i++) {
    var b = bones.get(i);
    out.push({name: b.getData().getName(), a: b.getA(), b: b.getB(), c: b.getC(),
              d: b.getD(), x: b.getWorldX(), y: b.getWorldY()});
}
Files.write(Paths.get(outPath), JSON.stringify(out).getBytes("UTF-8"));
print("wrote " + out.length + " bones to " + outPath);
