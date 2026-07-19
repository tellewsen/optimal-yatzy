interface YatzyEngineModule {
  queryJson(usedMask: number, upperTotal: number, diceCsv: string, rerollsLeft: number): string;
}

interface YatzyEngineModuleOptions {
  locateFile?: (path: string, prefix: string) => string;
}

export default function createYatzyModule(
  options?: YatzyEngineModuleOptions
): Promise<YatzyEngineModule>;
