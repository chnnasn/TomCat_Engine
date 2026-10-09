using TomCat;
namespace TomCat.Managed.Regression;
internal static unsafe partial class Program
{
    private static void VerifyUnityMessages()
    {
        var probe = new UnityMessageProbe();
        var entity = new Entity(SceneSession, 9, RuntimeGeneration);
        probe.__Bind(entity, new ScriptInstanceHandle(777));
        probe.__Create(); probe.__Enable(); probe.__Start();
        TimeRuntime.BeginFrame(0.025f);
        probe.__Update(0.025f);
        TimeRuntime.BeginFixedStep(0.01f);
        probe.__FixedUpdate(0.01f);
        Check(probe.FixedDelta == 0.01f && Time.inFixedTimeStep, "fixed deltaTime");
        TimeRuntime.EndFixedStep();
        probe.__LateUpdate(0.025f);
        Check(probe.FrameDelta == 0.025f && Time.deltaTime == 0.025f, "frame deltaTime restoration");
        Check(probe.Trace == "Awake Enable Start Update Fixed Late ", "private inherited messages");
        Check(probe.gameObject == new GameObject(entity), "GameObject identity");
        using (ScriptExecutionContext.Enter(entity, default))
            Check(probe.transform.Entity == entity, "transform alias");
        Vector3 vector = Vector3.right + Vector3.up; vector.z = 3;
        Check(vector.X == 1 && vector.y == 1 && vector.Z == 3, "vector aliases");
    }
    private abstract class UnityMessageBase : MonoBehaviour
    {
        public string Trace = "";
        private void Awake() => Trace += "Awake ";
    }
    private sealed class UnityMessageProbe : UnityMessageBase
    {
        public float FrameDelta, FixedDelta;
        private void OnEnable() => Trace += "Enable ";
        private void Start() => Trace += "Start ";
        private void Update() { FrameDelta = Time.deltaTime; Trace += "Update "; }
        private void FixedUpdate() { FixedDelta = Time.deltaTime; Trace += "Fixed "; }
        private void LateUpdate() => Trace += "Late ";
    }
}
