using TomCat;

namespace GameB;

public sealed class ProjectBBehaviour : TomCatBehaviour
{
	private static int s_staticCreates;

	public int ObservedStaticCreateSequence;

	protected override void OnCreate() =>
		ObservedStaticCreateSequence = ++s_staticCreates;
}
