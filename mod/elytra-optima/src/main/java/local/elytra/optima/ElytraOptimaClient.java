package local.elytra.optima;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonSyntaxException;
import com.mojang.blaze3d.platform.InputConstants;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.chat.Component;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public final class ElytraOptimaClient implements ClientModInitializer {
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
    private static final String CONFIG_FILE_NAME = "elytra-optima.json";
    private static final String DEFAULT_STRATEGY_ID = "start_plus_0";
    private static final String[] DEFAULT_CYCLE_ORDER = new String[] {
        "start_plus_0",
        "start_plus_2",
        "vx025_no_drop",
        "periodic_gain1",
        "smooth_max_climb",
        "jagged_max_climb_255",
        "hard_speed"
    };

    private static final Strategy START_PLUS_0 = Strategy.sampledResource(
        "start_plus_0",
        "起步+0（>32m）",
        "Start +0 (>32 m)",
        "start_plus_0.csv",
        208
    );
    private static final Strategy START_PLUS_2 = Strategy.sampledResource(
        "start_plus_2",
        "起步+2（>35m）",
        "Start +2 (>35 m)",
        "start_plus_2.csv",
        217
    );
    private static final Strategy VX025_NO_DROP = Strategy.sampledResource(
        "vx025_no_drop",
        "有初速不掉高（落差26m）",
        "Initial-speed no-drop (26 m span)",
        "vx025_no_drop.csv",
        170
    );
    private static final Strategy PERIODIC_GAIN1 = Strategy.sampledResource(
        "periodic_gain1",
        "高度+1（落差28m）",
        "Height +1 (28 m span)",
        "periodic_gain1.csv",
        179
    );
    private static final Strategy SMOOTH_MAX_CLIMB = Strategy.sampledResource(
        "smooth_max_climb",
        "平滑最大提升速度（20m/cycle，起步高度>75m）",
        "Smooth fastest climb (20 m/cycle, start >75 m)",
        "smooth_max_climb.csv",
        254
    );
    private static final Strategy JAGGED_MAX_CLIMB_255 = Strategy.sampledResource(
        "jagged_max_climb_255",
        "抖动最大提升速度（20m/cycle，起步高度>75m）",
        "Jittery fastest climb (20 m/cycle, start >75 m)",
        "jagged_max_climb_255.csv",
        255
    );
    private static final Strategy HARD_SPEED = Strategy.sampledResource(
        "hard_speed",
        "最快水平速度（33m/s，起步高度>142m）",
        "Fastest horizontal (33 m/s, start >142 m)",
        "hard_speed.csv",
        357
    );

    private static final Strategy[] ALL_STRATEGIES = new Strategy[] {
        START_PLUS_0,
        START_PLUS_2,
        VX025_NO_DROP,
        PERIODIC_GAIN1,
        SMOOTH_MAX_CLIMB,
        JAGGED_MAX_CLIMB_255,
        HARD_SPEED
    };
    private static final Map<String, Strategy> STRATEGIES_BY_ID = buildStrategyMap();

    private static KeyMapping toggleKey;
    private static KeyMapping cycleStrategyKey;
    private static boolean enabled = false;
    private static int strategyIndex = 0;
    private static int tickInCycle = 0;
    private static String defaultStrategyId = DEFAULT_STRATEGY_ID;
    private static List<Strategy> cycleStrategies = new ArrayList<>(Arrays.asList(ALL_STRATEGIES));

    @Override
    public void onInitializeClient() {
        applyConfig(loadConfig());

        toggleKey = KeyMappingHelper.registerKeyMapping(new KeyMapping(
            "key.elytra_optima.toggle",
            InputConstants.Type.KEYSYM,
            72,
            KeyMapping.Category.MISC
        ));
        cycleStrategyKey = KeyMappingHelper.registerKeyMapping(new KeyMapping(
            "key.elytra_optima.cycle_strategy",
            InputConstants.Type.KEYSYM,
            74,
            KeyMapping.Category.MISC
        ));

        ClientTickEvents.END_CLIENT_TICK.register(ElytraOptimaClient::onClientTick);
    }

    private static void onClientTick(Minecraft client) {
        while (toggleKey.consumeClick()) {
            enabled = !enabled;
            tickInCycle = 0;
            if (enabled) {
                strategyIndex = indexOfStrategy(defaultStrategyId);
            }
            boolean chinese = isChinese(client);
            showStatus(
                client,
                enabled
                    ? text(chinese, "鞘翅最优策略: 开 · 策略: ", "Elytra Optima: on · Strategy: ") + currentStrategy().label(chinese)
                    : text(chinese, "鞘翅最优策略: 关", "Elytra Optima: off")
            );
        }

        while (cycleStrategyKey.consumeClick()) {
            strategyIndex = (strategyIndex + 1) % cycleStrategies.size();
            tickInCycle = 0;
            boolean chinese = isChinese(client);
            showStatus(client, text(chinese, "策略: ", "Strategy: ") + currentStrategy().label(chinese));
        }

        if (client.isPaused()) {
            return;
        }

        LocalPlayer player = client.player;
        if (!enabled || player == null) {
            return;
        }

        if (!player.isFallFlying()) {
            tickInCycle = 0;
            return;
        }

        Strategy strategy = currentStrategy();
        double angle = strategy.angleAt(tickInCycle);
        player.setXRot((float) clamp(-angle, -90.0, 90.0));
        tickInCycle = strategy.nextTick(tickInCycle);
    }

    private static Strategy currentStrategy() {
        if (cycleStrategies.isEmpty()) {
            return START_PLUS_0;
        }
        strategyIndex = Math.floorMod(strategyIndex, cycleStrategies.size());
        return cycleStrategies.get(strategyIndex);
    }

    private static int indexOfStrategy(String strategyId) {
        for (int i = 0; i < cycleStrategies.size(); i++) {
            if (cycleStrategies.get(i).id().equals(strategyId)) {
                return i;
            }
        }
        return 0;
    }

    private static void showStatus(Minecraft client, String message) {
        if (client.player != null) {
            client.player.sendOverlayMessage(Component.literal(message));
        }
    }

    private static String text(boolean chinese, String zh, String en) {
        return chinese ? zh : en;
    }

    private static boolean isChinese(Minecraft client) {
        String selected = client.getLanguageManager().getSelected();
        return selected != null && selected.toLowerCase(Locale.ROOT).startsWith("zh");
    }

    private static double clamp(double value, double min, double max) {
        return Math.max(min, Math.min(max, value));
    }

    static List<StrategyInfo> strategyInfos(boolean chinese) {
        List<StrategyInfo> out = new ArrayList<>(ALL_STRATEGIES.length);
        for (Strategy strategy : ALL_STRATEGIES) {
            out.add(new StrategyInfo(strategy.id(), strategy.label(chinese)));
        }
        return out;
    }

    static List<String> configuredCycleOrderIds() {
        List<String> out = new ArrayList<>(cycleStrategies.size());
        for (Strategy strategy : cycleStrategies) {
            out.add(strategy.id());
        }
        return out;
    }

    static String configuredDefaultStrategyId() {
        return defaultStrategyId;
    }

    static boolean configureStrategies(String defaultId, List<String> cycleOrder) {
        ModConfig input = new ModConfig();
        input.defaultStrategy = defaultId;
        input.cycleOrder = new ArrayList<>(cycleOrder);
        ModConfig normalized = normalizeConfig(input);
        try {
            saveConfig(configPath(), normalized);
            applyConfig(normalized);
            return true;
        } catch (IOException exception) {
            return false;
        }
    }

    static boolean resetConfigToDefaults() {
        ModConfig defaults = ModConfig.defaults();
        try {
            saveConfig(configPath(), defaults);
            applyConfig(defaults);
            return true;
        } catch (IOException exception) {
            return false;
        }
    }

    private static Map<String, Strategy> buildStrategyMap() {
        Map<String, Strategy> out = new LinkedHashMap<>();
        for (Strategy strategy : ALL_STRATEGIES) {
            if (out.put(strategy.id(), strategy) != null) {
                throw new IllegalStateException("Duplicate Elytra Optima strategy id: " + strategy.id());
            }
        }
        return Collections.unmodifiableMap(out);
    }

    private static ModConfig loadConfig() {
        ModConfig defaults = ModConfig.defaults();
        Path path = configPath();
        try {
            Files.createDirectories(path.getParent());
            if (!Files.exists(path)) {
                saveConfig(path, defaults);
                return defaults;
            }
            try (Reader reader = Files.newBufferedReader(path, StandardCharsets.UTF_8)) {
                ModConfig loaded = GSON.fromJson(reader, ModConfig.class);
                ModConfig normalized = normalizeConfig(loaded);
                saveConfig(path, normalized);
                return normalized;
            }
        } catch (IOException | JsonSyntaxException exception) {
            return defaults;
        }
    }

    private static Path configPath() {
        return FabricLoader.getInstance().getConfigDir().resolve(CONFIG_FILE_NAME);
    }

    private static void applyConfig(ModConfig config) {
        defaultStrategyId = config.defaultStrategy;
        cycleStrategies = new ArrayList<>();
        for (String id : config.cycleOrder) {
            Strategy strategy = STRATEGIES_BY_ID.get(id);
            if (strategy != null) {
                cycleStrategies.add(strategy);
            }
        }
        if (cycleStrategies.isEmpty()) {
            cycleStrategies.add(START_PLUS_0);
            defaultStrategyId = START_PLUS_0.id();
        }
        strategyIndex = indexOfStrategy(defaultStrategyId);
    }

    private static ModConfig normalizeConfig(ModConfig input) {
        if (input == null) {
            return ModConfig.defaults();
        }

        String defaultId = STRATEGIES_BY_ID.containsKey(input.defaultStrategy)
            ? input.defaultStrategy
            : DEFAULT_STRATEGY_ID;
        List<String> order = normalizeCycleOrder(input.cycleOrder);
        if (!order.contains(defaultId)) {
            order.add(0, defaultId);
        }

        ModConfig output = new ModConfig();
        output.defaultStrategy = defaultId;
        output.cycleOrder = order;
        return output;
    }

    private static List<String> normalizeCycleOrder(List<String> input) {
        List<String> output = new ArrayList<>();
        if (input != null) {
            for (String id : input) {
                if (STRATEGIES_BY_ID.containsKey(id) && !output.contains(id)) {
                    output.add(id);
                }
            }
        }
        if (output.isEmpty()) {
            output.addAll(Arrays.asList(DEFAULT_CYCLE_ORDER));
        }
        return output;
    }

    private static void saveConfig(Path path, ModConfig config) throws IOException {
        Files.createDirectories(path.getParent());
        try (Writer writer = Files.newBufferedWriter(path, StandardCharsets.UTF_8)) {
            GSON.toJson(config, writer);
        }
    }

    private static double[] loadAngleCsv(String resourceName, int expectedTicks) {
        String resourcePath = "/assets/elytra_optima/strategies/" + resourceName;
        try (
            InputStream stream = ElytraOptimaClient.class.getResourceAsStream(resourcePath);
            BufferedReader reader = stream == null
                ? null
                : new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8))
        ) {
            if (reader == null) {
                throw new IllegalStateException("Missing Elytra Optima strategy resource: " + resourcePath);
            }

            String header = reader.readLine();
            int angleColumn = findAngleColumn(header, resourcePath);
            List<Double> angles = new ArrayList<>();
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.isBlank()) {
                    continue;
                }
                String[] parts = splitCsvLine(line);
                if (angleColumn >= parts.length) {
                    throw new IllegalStateException("Malformed Elytra Optima strategy row in " + resourcePath + ": " + line);
                }
                angles.add(Double.parseDouble(parts[angleColumn]));
            }

            if (angles.size() != expectedTicks) {
                throw new IllegalStateException(
                    resourcePath + " has " + angles.size() + " ticks, expected " + expectedTicks
                );
            }

            double[] out = new double[angles.size()];
            for (int i = 0; i < out.length; i++) {
                out[i] = angles.get(i);
            }
            return out;
        } catch (IOException exception) {
            throw new IllegalStateException("Failed to load Elytra Optima strategy resource: " + resourcePath, exception);
        }
    }

    private static int findAngleColumn(String header, String resourcePath) {
        if (header == null) {
            throw new IllegalStateException("Empty Elytra Optima strategy resource: " + resourcePath);
        }
        String[] columns = splitCsvLine(header);
        for (int i = 0; i < columns.length; i++) {
            String column = columns[i];
            if ("angle".equals(column) || "angleDeg_pass_to_stepElytra2D".equals(column)) {
                return i;
            }
        }
        throw new IllegalStateException("No angle column in Elytra Optima strategy resource: " + resourcePath);
    }

    private static String[] splitCsvLine(String line) {
        String[] parts = line.split(",", -1);
        for (int i = 0; i < parts.length; i++) {
            parts[i] = parts[i].trim().replace("\"", "");
        }
        return parts;
    }

    private static final class ModConfig {
        String defaultStrategy = DEFAULT_STRATEGY_ID;
        List<String> cycleOrder = new ArrayList<>(Arrays.asList(DEFAULT_CYCLE_ORDER));

        static ModConfig defaults() {
            ModConfig config = new ModConfig();
            config.defaultStrategy = DEFAULT_STRATEGY_ID;
            config.cycleOrder = new ArrayList<>(Arrays.asList(DEFAULT_CYCLE_ORDER));
            return config;
        }
    }

    record StrategyInfo(String id, String label) {
    }

    private record Strategy(String id, String zhLabel, String enLabel, double[] angles) {
        static Strategy sampledResource(
            String id,
            String zhLabel,
            String enLabel,
            String resourceName,
            int expectedTicks
        ) {
            return new Strategy(id, zhLabel, enLabel, loadAngleCsv(resourceName, expectedTicks));
        }

        String label(boolean chinese) {
            return chinese ? zhLabel : enLabel;
        }

        double angleAt(int tick) {
            return angles[Math.floorMod(tick, angles.length)];
        }

        int nextTick(int tick) {
            return (tick + 1) % angles.length;
        }
    }
}
