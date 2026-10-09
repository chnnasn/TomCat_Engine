using TomCat;

namespace GameB;

public sealed class ProjectBBehaviour : MonoBehaviour
{
	private static int s_staticCreates;

	public int ObservedStaticCreateSequence;

	private void Awake() =>
		ObservedStaticCreateSequence = ++s_staticCreates;
}
